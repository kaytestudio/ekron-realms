package com.example.realms

data class CatalogItem(val nativeType: Int, val label: String, val asset: String, val defaultSurface: Int = 1)

object BuildCatalog {
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
        CatalogItem(8, "УАЗ-469", "catalog/rendered/uaz.webp",-1),
        CatalogItem(12, "Куб 1×1×1", "textures/concrete.png",1),
        CatalogItem(13, "Деревянный ящик", "textures/wood.png",5),
        CatalogItem(14, "Металлическая бочка", "textures/rust.png",3),
        CatalogItem(15, "Деревянный поддон", "textures/wood.png",5),
        CatalogItem(16, "Связка стальных труб", "textures/rust.png",3),
        CatalogItem(17, "Строительный брус", "textures/wood.png",5),
        CatalogItem(18, "Мешок цемента", "textures/concrete.png",1),
        CatalogItem(19, "Строительные леса", "textures/rust.png",3),
        CatalogItem(20, "Дорожный конус", "textures/rust.png",3),
        CatalogItem(21, "Уличная урна", "textures/rust.png",3),
        CatalogItem(22, "Уличная скамейка", "textures/wood.png",5),
        CatalogItem(23, "Люк", "textures/rust.png",3),
        CatalogItem(24, "Бетонный барьер", "textures/concrete.png",1),
        CatalogItem(25, "Шина", "textures/rust.png",3),
        CatalogItem(26, "Столб ограждения", "textures/rust.png",3),
        CatalogItem(27, "Стальная балка", "textures/rust.png",3),
        CatalogItem(28, "Мусорный контейнер", "textures/rust.png",3),
        CatalogItem(29, "Взрывная бочка", "textures/rust.png",-1),
        CatalogItem(30, "Малый деревянный ящик", "textures/wood.png",5),
        CatalogItem(31, "Большой деревянный ящик", "textures/wood.png",5),
        CatalogItem(32, "Малый металлический ящик", "textures/rust.png",3),
        CatalogItem(33, "Большой металлический ящик", "textures/rust.png",3),
        CatalogItem(34, "Бетонный блок малый", "textures/concrete.png",1),
        CatalogItem(35, "Бетонный блок большой", "textures/concrete.png",1)
    )
}
