#include "test_support.h"

#include "presentation/renderer.h"

#include <d2d1.h>

TEST_CASE(renderer_state_recreates_resources_after_device_loss) {
    desktop_todo::RendererState state;
    EXPECT_TRUE(!state.resources_ready());
    state.mark_resources_created();
    EXPECT_TRUE(state.resources_ready());
    const auto generation = state.generation();

    EXPECT_TRUE(state.finish_draw(D2DERR_RECREATE_TARGET));
    EXPECT_TRUE(!state.resources_ready());
    state.mark_resources_created();
    EXPECT_TRUE(state.generation() > generation);
}

TEST_CASE(renderer_state_keeps_resources_after_successful_draw) {
    desktop_todo::RendererState state;
    state.mark_resources_created();
    EXPECT_TRUE(!state.finish_draw(S_OK));
    EXPECT_TRUE(state.resources_ready());
}
