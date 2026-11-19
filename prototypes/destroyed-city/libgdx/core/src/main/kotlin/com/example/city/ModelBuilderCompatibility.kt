package com.example.city

import com.badlogic.gdx.graphics.g3d.Material
import com.badlogic.gdx.graphics.g3d.Model
import com.badlogic.gdx.graphics.g3d.utils.ModelBuilder

// Kotlin does not implicitly widen the original generator's Int usage flags.
// Keep that supplied source unchanged; delegate to LibGDX's Long API here.
internal fun ModelBuilder.createBox(
    width: Float, height: Float, depth: Float, material: Material, attributes: Int
): Model = createBox(width, height, depth, material, attributes.toLong())

internal fun ModelBuilder.createCylinder(
    width: Float, height: Float, depth: Float, divisions: Int,
    material: Material, attributes: Int
): Model = createCylinder(width, height, depth, divisions, material, attributes.toLong())
