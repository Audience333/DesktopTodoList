/* =============================================================================
 * host.js — 宿主通信层
 * -----------------------------------------------------------------------------
 * 页面 ↔ PowerShell 宿主 通过本地回环 HTTP 通信（宿主即本地静态服务器）。
 *
 *   页面 → 宿主：POST /__host/cmd?t=<token>   （批量指令，宿主消费后回 $true）
 *   宿主 → 页面：GET  /__host/resp?t=<token>  （状态回执：能力、窗口边界、错误）
 *
 * 为什么不用 localStorage 传指令：Electron 之外的场景下，宿主读 Chromium 的
 * localStorage 需要解析 LevelDB（含 Snappy 压缩），代价高且脆弱。HTTP 通道
 * 双向、可校验、可观测，且不引入额外端口（复用静态服务器）。
 *
 * 窗口"状态"类偏好（层级/穿透/浮窗/自启）走 hello 指令一次性同步，
 * 宿主同时落盘到 host-settings.json，保证下次启动时页面还没跑起来就能恢复。
 * ========================================================================== */
(function (global) {
  'use strict';

  var App = global.TodoApp = global.TodoApp || {};

  var FLUSH_MS = 120;    // 指令批量下发间隔
  var POLL_MS = 400;     // 回执轮询间隔
  var TIMEOUT_MS = 4000; // 单次请求超时

  function Host() {
    this.available = false;
    this.token = null;
    this.base = null;
    this.caps = {
      topmost: false, bottom: false, clickThrough: false, autostart: false, tray: false
    };
    this.bounds = null;
    this.hwnd = null;
    this.lastSeen = 0;
    this.lastError = null;
    this.degradedReason = null;
    this._queue = [];
    this._flushTimer = null;
    this._pollTimer = null;
    this._listeners = [];
    this._inflight = false;
    this._inbox = null;      // { layer, selectable, autoStart, ... } 由宿主回传的实际状态
  }

  Host.prototype = {

    init: function () {
      var params = new URLSearchParams(global.location.search);
      this.available = params.get('host') === '1';
      this.token = params.get('t');

      var port = params.get('p');
      this.base = port
        ? (global.location.protocol + '//' + global.location.hostname + ':' + port)
        : global.location.origin;

      if (!this.available || !this.token) {
        this.available = false;
        return this;
      }

      this._pollTimer = global.setInterval(this._poll.bind(this), POLL_MS);
      this._flushTimer = global.setInterval(this._flush.bind(this), FLUSH_MS);
      this._poll();
      return this;
    },

    onUpdate: function (fn) { this._listeners.push(fn); },

    _emit: function () {
      for (var i = 0; i < this._listeners.length; i++) {
        try { this._listeners[i](this); } catch (e) {
          if (global.console) console.error('[host] 监听器异常', e);
        }
      }
    },

    _url: function (sub) {
      return this.base + '/__host/' + sub + '?t=' + encodeURIComponent(this.token);
    },

    /** 带超时的 fetch，宿主未响应时不会让页面一直挂着 */
    _fetch: function (url, options) {
      if (typeof global.fetch !== 'function') {
        return Promise.reject(new Error('当前环境不支持 fetch'));
      }
      var ctrl = ('AbortController' in global) ? new AbortController() : null;
      var opts = options || {};
      if (ctrl) opts.signal = ctrl.signal;
      var timer = ctrl ? global.setTimeout(function () { ctrl.abort(); }, TIMEOUT_MS) : null;

      return global.fetch(url, opts).then(function (res) {
        if (timer) global.clearTimeout(timer);
        if (!res.ok) throw new Error('HTTP ' + res.status);
        return res.json();
      }, function (err) {
        if (timer) global.clearTimeout(timer);
        throw err;
      });
    },

    /* ------------------------------------------------------------ 下发指令 */

    /** 入队一条指令；同一帧内的多条指令会在下一次 flush 合并发送 */
    send: function (cmd, payload) {
      if (!this.available) return false;
      this._queue.push({ cmd: cmd, payload: payload || null, at: Date.now() });
      // 队列异常积压说明宿主没在消费，丢弃旧指令并提示
      if (this._queue.length > 200) {
        this._queue = this._queue.slice(-50);
      }
      return true;
    },

    _flush: function () {
      if (!this.available || this._inflight || !this._queue.length) return;
      var batch = this._queue;
      this._queue = [];
      this._inflight = true;

      var self = this;
      this._fetch(this._url('cmd'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(batch)
      }).then(function () {
        self._inflight = false;
        self._failCount = 0;
      }, function (err) {
        self._inflight = false;
        self._failCount = (self._failCount || 0) + 1;
        // 连续失败说明宿主已退出：把指令放回队首，等待降级提示
        self._queue = batch.concat(self._queue);
        if (self._failCount >= 5) self._degrade('宿主无响应');
      });
    },

    _poll: function () {
      if (!this.available) return;
      var self = this;
      this._fetch(this._url('resp')).then(function (resp) {
        self._failCount = 0;
        if (!resp || typeof resp !== 'object') return;
        var at = resp.at || 0;
        if (at <= self.lastSeen) return;
        self.lastSeen = at;
        self._apply(resp);
      }, function (err) {
        self._failCount = (self._failCount || 0) + 1;
        if (self._failCount >= 6) self._degrade('宿主无响应');
      });
    },

    _apply: function (resp) {
      if (resp.caps) {
        for (var k in resp.caps) {
          if (Object.prototype.hasOwnProperty.call(resp.caps, k)) this.caps[k] = !!resp.caps[k];
        }
      }
      if (Array.isArray(resp.recentErrors) && resp.recentErrors.length) {
        this.lastError = resp.recentErrors[resp.recentErrors.length - 1];
      }
      if (typeof resp.hwnd !== 'undefined') this.hwnd = resp.hwnd;
      this.bounds = resp.bounds || null;
      // 宿主回传的"实际状态"：托盘菜单改了层级/穿透后，页面据此校准 UI
      if (resp.state) this._inbox = resp.state;
      this._emit();
    },

    /** 取出宿主侧的状态变更（消费一次即清空） */
    takeInbox: function () {
      var v = this._inbox;
      this._inbox = null;
      return v;
    },

    _degrade: function (reason) {
      if (!this.available) return;
      this.available = false;
      this.degradedReason = reason;
      if (this._pollTimer) { global.clearInterval(this._pollTimer); this._pollTimer = null; }
      if (this._flushTimer) { global.clearInterval(this._flushTimer); this._flushTimer = null; }
      this._emit();
    },

    /* ------------------------------------------------------------ 语义化 API */

    /**
     * 把全部窗口相关偏好一次性同步给宿主，并请求宿主据此应用。
     * 页面启动时调用一次，之后每次改设置再单独下发对应指令。
     */
    hello: function (settings) {
      return this.send('hello', {
        mode: settings.windowMode,
        layer: settings.windowLayer,
        selectable: settings.selectable,
        autoStart: settings.autoStart,
        startMinimized: settings.startMinimized,
        geometry: settings.floatingGeometry,
        dpr: global.devicePixelRatio || 1,
        hotkey: settings.hotkey,
        hotkeySelectable: settings.hotkeySelectable
      });
    },

    setLayer: function (layer) { return this.send('set-layer', { layer: layer }); },
    setSelectable: function (selectable) { return this.send('set-selectable', { selectable: !!selectable }); },
    setMode: function (mode) { return this.send('set-mode', { mode: mode }); },
    setAutoStart: function (enabled) { return this.send('set-autostart', { enabled: !!enabled }); },
    setStartMinimized: function (v) { return this.send('set-start-minimized', { enabled: !!v }); },

    /** 相对位移移动窗口（CSS px，宿主按 dpr 换算为物理像素） */
    moveBy: function (dx, dy) {
      return this.send('move-by', { dx: dx, dy: dy, dpr: global.devicePixelRatio || 1 });
    },

    resizeBy: function (dw, dh) {
      return this.send('resize-by', { dw: dw, dh: dh, dpr: global.devicePixelRatio || 1 });
    },

    /** 拖拽结束后用绝对几何校正，消除相对位移的累计误差 */
    commitGeometry: function () {
      return this.send('commit-geometry', { dpr: global.devicePixelRatio || 1 });
    },

    /** 显示待办数量到托盘提示 */
    reportCounts: function (counts) {
      return this.send('counts', { pending: counts.all, overdue: counts.overdue });
    },

    minimize: function () { return this.send('minimize'); },
    hide: function () { return this.send('hide'); },
    close: function () { return this.send('close'); },
    toggleVisible: function () { return this.send('toggle-visible'); },
    refresh: function () { return this.send('refresh'); },

    /**
     * 把宿主回报的窗口边界写回本地 settings。
     * 宿主自己也会落一份 host-settings.json（页面未启动时用于恢复），
     * 这里写本地副本是为了让设置面板/导出数据里也能看到几何信息。
     */
    persistGeometryToLocal: function (store) {
      if (!store || !this.bounds) return;
      var dpr = global.devicePixelRatio || 1;
      store.setSettings({
        floatingGeometry: {
          x: Math.round(this.bounds.x / dpr),
          y: Math.round(this.bounds.y / dpr),
          w: Math.round(this.bounds.w / dpr),
          h: Math.round(this.bounds.h / dpr)
        }
      }, true);
    },

    destroy: function () {
      if (this._pollTimer) { global.clearInterval(this._pollTimer); this._pollTimer = null; }
      if (this._flushTimer) { global.clearInterval(this._flushTimer); this._flushTimer = null; }
      this._listeners = [];
    }
  };

  App.Host = Host;
})(window);
