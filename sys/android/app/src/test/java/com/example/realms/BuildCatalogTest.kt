package com.example.realms
import org.junit.Assert.*
import org.junit.Test
class BuildCatalogTest {
    @Test fun spawnPreviewsAllowModelOwnedMaterials() {
        val images=arrayOfNulls<String>(7)
        images[1]="concrete";images[3]="metal";images[5]="wood";images[6]="fabric"
        for(category in 0..3)for(index in BuildCatalog.indicesForCategory(category)){
            val item=BuildCatalog.entries[index]
            val preview=BuildCatalog.materialPreview(images,item.defaultSurface)
            if(item.defaultSurface==-1)assertNull(preview)
            else assertEquals(images[item.defaultSurface],preview)
        }
        val barrel=BuildCatalog.entries.single { it.nativeType==29 }
        assertEquals(-1,barrel.defaultSurface)
        assertNull(BuildCatalog.materialPreview(images,barrel.defaultSurface))
        assertNull(BuildCatalog.materialPreview(images,Int.MIN_VALUE))
        assertNull(BuildCatalog.materialPreview(images,images.size))
    }
    @Test fun picturesMatchNativeTypesWithoutDuplicateAssets() {
        val items=BuildCatalog.entries
        assertEquals(listOf(0,1,2,3,4,5,6,7,9,10,11,8,12)+(13..47).toList(),items.map { it.nativeType })
        assertEquals(48,items.map { it.asset }.toSet().size)
        assertEquals(listOf(R.string.item_concrete_block,R.string.item_floor_slab,R.string.item_window,R.string.item_door,R.string.item_table,R.string.item_chair,R.string.item_cabinet,R.string.item_sofa,R.string.item_wood_block,R.string.item_brick_block,R.string.item_metal_block,R.string.item_uaz,R.string.item_cube,R.string.item_crate,R.string.item_barrel,R.string.item_pallet,R.string.item_pipes,R.string.item_plank,R.string.item_cement_bag,R.string.item_scaffold,R.string.item_cone,R.string.item_bin,R.string.item_bench,R.string.item_manhole,R.string.item_barrier,R.string.item_tire,R.string.item_post,R.string.item_beam,R.string.item_dumpster,R.string.item_explosive_barrel,R.string.item_crate_wood_small,R.string.item_crate_wood_large,R.string.item_crate_metal_small,R.string.item_crate_metal_large,R.string.item_block_small,R.string.item_block_large,R.string.item_sandbags,R.string.item_razor_wire,R.string.item_hedgehog,R.string.item_ammo_crate,R.string.item_shell_crate,R.string.item_dragon_tooth,R.string.item_jerrycan,R.string.item_tent,R.string.item_camo_net,R.string.item_supply_stack,R.string.item_fuel_drum,R.string.item_medkit),items.map { it.labelRes })
        assertEquals(listOf(1,1,1,5,5,5,5,6,5,2,3,-1,1,5,3,5,3,5,1,3,3,3,5,3,1,3,3,3,3,-1,5,5,3,3,1,1)+List(12){-1},items.map { it.defaultSurface })
        assertTrue(items.all { it.asset.startsWith("catalog/") || it.asset.startsWith("textures/") })
    }
}
