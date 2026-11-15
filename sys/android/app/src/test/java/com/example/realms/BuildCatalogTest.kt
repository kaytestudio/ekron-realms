package com.example.realms
import org.junit.Assert.*
import org.junit.Test
class BuildCatalogTest {
    @Test fun picturesMatchNativeTypesWithoutDuplicateAssets() {
        val items=BuildCatalog.entries
        assertEquals((0..7).toList(),items.map { it.nativeType })
        assertEquals(8,items.map { it.asset }.toSet().size)
        assertEquals(listOf("Стена","Пол","Окно","Дверь","Стол","Стул","Шкаф","Диван"),items.map { it.label })
        assertEquals(listOf(1,1,1,5,5,5,5,6),items.map { it.defaultSurface })
        assertTrue(items.all { it.asset.startsWith("catalog/") && it.asset.endsWith(".webp") })
    }
}
