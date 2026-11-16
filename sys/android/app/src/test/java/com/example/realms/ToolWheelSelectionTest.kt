package com.example.realms

import org.junit.Assert.*
import org.junit.Test

class ToolWheelSelectionTest {
    @Test fun exactlyThreeExistingTools() {
        assertEquals(listOf(HandTool.GRAVITY_GUN, HandTool.SPAWN_GUN, HandTool.HAND_EMPTY), ToolWheelSelection.tools)
        assertFalse(ToolWheelSelection.tools.contains(HandTool.DELETE_TOOL))
    }
    @Test fun swipeDirectionsMatchSectorCenters() {
        assertEquals(0, ToolWheelSelection.sector(0f, -100f, 20f))
        assertEquals(1, ToolWheelSelection.sector(87f, 50f, 20f))
        assertEquals(2, ToolWheelSelection.sector(-87f, 50f, 20f))
    }
    @Test fun tapAndReturnToCenterCancelSelection() {
        assertNull(ToolWheelSelection.sector(0f, 0f, 20f))
        assertNull(ToolWheelSelection.sector(20f, 0f, 20f))
        assertNull(ToolWheelSelection.sector(Float.NaN, 0f, 20f))
    }
    @Test fun farSwipesRemainSelectedAndWrapAtLeft() {
        assertEquals(0, ToolWheelSelection.sector(-100f, -1000f, 20f))
        assertEquals(2, ToolWheelSelection.sector(-1000f, -1f, 20f))
        assertEquals(2, ToolWheelSelection.sector(-1000f, 1f, 20f))
    }
}
