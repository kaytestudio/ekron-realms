package com.example.realms

enum class HandTool { HAND_EMPTY, SPAWN_GUN, GRAVITY_GUN, DELETE_TOOL }

/** Pure state reducer; held state is confirmed by the native world, never guessed on tap. */
class ToolState {
    var tool = HandTool.HAND_EMPTY
        private set
    var holding = false
        private set
    fun select(next: HandTool) { tool = next; holding = false }
    fun confirmHolding(value: Boolean) { holding = tool == HandTool.GRAVITY_GUN && value }
    val spawnVisible get() = tool == HandTool.SPAWN_GUN
    val gravityVisible get() = tool == HandTool.GRAVITY_GUN
    val rotateHeldVisible get() = gravityVisible && holding
    val deleteVisible get() = tool == HandTool.DELETE_TOOL
    val grabCaption get() = if (holding) "Бросить" else "Захват"
}
