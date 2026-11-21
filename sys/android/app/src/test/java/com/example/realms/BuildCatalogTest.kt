package com.example.realms
import org.junit.Assert.*
import org.junit.Test
class BuildCatalogTest {
    @Test fun picturesMatchNativeTypesWithoutDuplicateAssets() {
        val items=BuildCatalog.entries
        assertEquals(listOf(0,1,2,3,4,5,6,7,9,10,11,8,12),items.map { it.nativeType })
        assertEquals(13,items.map { it.asset }.toSet().size)
        assertEquals(listOf(R.string.item_concrete_block,R.string.item_floor_slab,R.string.item_window,R.string.item_door,R.string.item_table,R.string.item_chair,R.string.item_cabinet,R.string.item_sofa,R.string.item_wood_block,R.string.item_brick_block,R.string.item_metal_block,R.string.item_uaz,R.string.item_cube),items.map { it.labelRes })
        assertEquals(listOf(1,1,1,5,5,5,5,6,5,2,3,-1,1),items.map { it.defaultSurface })
        assertTrue(items.all { it.asset.startsWith("catalog/") || it.asset.startsWith("textures/") })
    }
}
