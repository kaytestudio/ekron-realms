package com.example.realms

import androidx.annotation.StringRes

/** [labelRes] is a localized string resource (values/, values-ru/, values-pt-rPT/, values-es-rES/). */
data class CatalogItem(val nativeType: Int, @StringRes val labelRes: Int, val asset: String, val defaultSurface: Int = 1)

object BuildCatalog {
    // -1 means the model owns its materials, never an array index.
    fun <T> materialPreview(images: Array<T>, surface: Int): T? = images.getOrNull(surface)
    // Vehicle stays available under construction; no invented weapons/drones.
    fun indicesForCategory(category: Int): List<Int> = entries.indices.filter {
        when(category) {
            0 -> entries[it].nativeType !in setOf(4,5,6,7,13,18,22,25,28)
            1 -> entries[it].nativeType in setOf(4,5,6,7,13,18,22,25,28)
            else -> false
        }
    }
    // IDs match World::bounds and the spawn-gun selection protocol.
    val entries = listOf(
        CatalogItem(0, R.string.item_concrete_block, "catalog/rendered/wall.webp"),
        CatalogItem(1, R.string.item_floor_slab, "catalog/rendered/floor.webp"),
        CatalogItem(2, R.string.item_window, "catalog/rendered/window.webp"),
        CatalogItem(3, R.string.item_door, "catalog/rendered/door.webp",5),
        CatalogItem(4, R.string.item_table, "catalog/rendered/table.webp",5),
        CatalogItem(5, R.string.item_chair, "catalog/rendered/chair.webp",5),
        CatalogItem(6, R.string.item_cabinet, "catalog/rendered/cabinet.webp",5),
        CatalogItem(7, R.string.item_sofa, "catalog/rendered/sofa.webp",6),
        CatalogItem(9, R.string.item_wood_block, "catalog/rendered/wood_block.webp",5),
        CatalogItem(10, R.string.item_brick_block, "catalog/rendered/brick_block.webp",2),
        CatalogItem(11, R.string.item_metal_block, "catalog/rendered/metal_block.webp",3),
        CatalogItem(8, R.string.item_uaz, "catalog/rendered/uaz.webp",-1),
        CatalogItem(12, R.string.item_cube, "catalog/rendered/cube.webp",1),
        CatalogItem(13, R.string.item_crate, "catalog/rendered/crate.webp",5),
        CatalogItem(14, R.string.item_barrel, "catalog/rendered/barrel.webp",3),
        CatalogItem(15, R.string.item_pallet, "catalog/rendered/pallet.webp",5),
        CatalogItem(16, R.string.item_pipes, "catalog/rendered/pipes.webp",3),
        CatalogItem(17, R.string.item_plank, "catalog/rendered/plank.webp",5),
        CatalogItem(18, R.string.item_cement_bag, "catalog/rendered/cement_bag.webp",1),
        CatalogItem(19, R.string.item_scaffold, "catalog/rendered/scaffold.webp",3),
        CatalogItem(20, R.string.item_cone, "catalog/rendered/cone.webp",3),
        CatalogItem(21, R.string.item_bin, "catalog/rendered/bin.webp",3),
        CatalogItem(22, R.string.item_bench, "catalog/rendered/bench.webp",5),
        CatalogItem(23, R.string.item_manhole, "catalog/rendered/manhole.webp",3),
        CatalogItem(24, R.string.item_barrier, "catalog/rendered/barrier.webp",1),
        CatalogItem(25, R.string.item_tire, "catalog/rendered/tire.webp",3),
        CatalogItem(26, R.string.item_post, "catalog/rendered/post.webp",3),
        CatalogItem(27, R.string.item_beam, "catalog/rendered/beam.webp",3),
        CatalogItem(28, R.string.item_dumpster, "catalog/rendered/dumpster.webp",3),
        CatalogItem(29, R.string.item_explosive_barrel, "catalog/rendered/explosive_barrel.webp",-1),
        CatalogItem(30, R.string.item_crate_wood_small, "catalog/rendered/crate_wood_small.webp",5),
        CatalogItem(31, R.string.item_crate_wood_large, "catalog/rendered/crate_wood_large.webp",5),
        CatalogItem(32, R.string.item_crate_metal_small, "catalog/rendered/crate_metal_small.webp",3),
        CatalogItem(33, R.string.item_crate_metal_large, "catalog/rendered/crate_metal_large.webp",3),
        CatalogItem(34, R.string.item_block_small, "catalog/rendered/block_small.webp",1),
        CatalogItem(35, R.string.item_block_large, "catalog/rendered/block_large.webp",1),
        CatalogItem(36, R.string.item_sandbags, "catalog/rendered/sandbags.webp",-1),
        CatalogItem(37, R.string.item_razor_wire, "catalog/rendered/razor_wire.webp",-1),
        CatalogItem(38, R.string.item_hedgehog, "catalog/rendered/hedgehog.webp",-1),
        CatalogItem(39, R.string.item_ammo_crate, "catalog/rendered/ammo_crate.webp",-1),
        CatalogItem(40, R.string.item_shell_crate, "catalog/rendered/shell_crate.webp",-1),
        CatalogItem(41, R.string.item_dragon_tooth, "catalog/rendered/dragon_tooth.webp",-1),
        CatalogItem(42, R.string.item_jerrycan, "catalog/rendered/jerrycan.webp",-1),
        CatalogItem(43, R.string.item_tent, "catalog/rendered/tent.webp",-1),
        CatalogItem(44, R.string.item_camo_net, "catalog/rendered/camo_net.webp",-1),
        CatalogItem(45, R.string.item_supply_stack, "catalog/rendered/supply_stack.webp",-1),
        CatalogItem(46, R.string.item_fuel_drum, "catalog/rendered/fuel_drum.webp",-1),
        CatalogItem(47, R.string.item_medkit, "catalog/rendered/medkit.webp",-1)
    )
}
