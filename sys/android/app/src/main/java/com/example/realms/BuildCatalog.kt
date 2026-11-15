package com.example.realms

data class CatalogItem(val nativeType: Int, val label: String, val asset: String, val defaultSurface: Int = 1)

object BuildCatalog {
    // IDs match World::bounds and the spawn-gun selection protocol.
    val entries = listOf(
        CatalogItem(0, "Бетонный блок", "catalog/rendered/wall.webp"),
        CatalogItem(1, "Плита (пол)", "catalog/rendered/floor.webp"),
        CatalogItem(2, "Окно", "catalog/rendered/window.webp"),
        CatalogItem(3, "Дверь", "catalog/rendered/door.webp",5),
        CatalogItem(4, "Стол", "catalog/rendered/table.webp",5),
        CatalogItem(5, "Стул", "catalog/rendered/chair.webp",5),
        CatalogItem(6, "Шкаф", "catalog/rendered/cabinet.webp",5),
        CatalogItem(7, "Диван", "catalog/rendered/sofa.webp",6),
        CatalogItem(9, "Деревянный блок", "textures/wood.png",5),
        CatalogItem(10, "Кирпичный блок", "textures/brick.png",2),
        CatalogItem(11, "Металлический блок", "textures/rust.png",3),
        CatalogItem(8, "УАЗ-469", "catalog/rendered/uaz.webp",-1)
    )
}
