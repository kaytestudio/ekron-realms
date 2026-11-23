package com.example.realms

import androidx.annotation.StringRes

/** [labelRes] is a localized string resource (values/, values-ru/, values-pt-rPT/, values-es-rES/). */
data class CatalogItem(val nativeType: Int, @StringRes val labelRes: Int, val asset: String, val defaultSurface: Int = 1)

object BuildCatalog {
    // Vehicle stays available under construction; no invented weapons/drones.
    fun indicesForCategory(category: Int): List<Int> = entries.indices.filter {
        when(category) {
            0 -> entries[it].nativeType !in 4..7
            1 -> entries[it].nativeType in 4..7
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
        CatalogItem(9, R.string.item_wood_block, "textures/wood.png",5),
        CatalogItem(10, R.string.item_brick_block, "textures/brick.png",2),
        CatalogItem(11, R.string.item_metal_block, "textures/rust.png",3),
        CatalogItem(8, R.string.item_uaz, "catalog/rendered/uaz.webp",-1),
        CatalogItem(12, R.string.item_cube, "textures/concrete.png",1)
    )
}
