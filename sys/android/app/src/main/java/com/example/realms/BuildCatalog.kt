package com.example.realms

data class CatalogItem(val nativeType: Int, val label: String, val asset: String)

object BuildCatalog {
    // IDs match World::bounds and the spawn-gun selection protocol.
    val entries = listOf(
        CatalogItem(0, "Стена", "catalog/wall.webp"),
        CatalogItem(1, "Пол", "catalog/floor.webp"),
        CatalogItem(2, "Окно", "catalog/window.webp"),
        CatalogItem(3, "Дверь", "catalog/door.webp"),
        CatalogItem(4, "Стол", "catalog/table.webp"),
        CatalogItem(5, "Стул", "catalog/chair.webp"),
        CatalogItem(6, "Шкаф", "catalog/cabinet.webp"),
        CatalogItem(7, "Диван", "catalog/sofa.webp")
    )
}
