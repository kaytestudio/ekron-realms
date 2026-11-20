package com.example.city

import com.badlogic.gdx.graphics.Color
import com.badlogic.gdx.graphics.GL20
import com.badlogic.gdx.graphics.PerspectiveCamera
import com.badlogic.gdx.graphics.VertexAttributes
import com.badlogic.gdx.graphics.g3d.Material
import com.badlogic.gdx.graphics.g3d.Model
import com.badlogic.gdx.graphics.g3d.ModelBatch
import com.badlogic.gdx.graphics.g3d.ModelInstance
import com.badlogic.gdx.graphics.g3d.attributes.ColorAttribute
import com.badlogic.gdx.graphics.g3d.utils.ModelBuilder
import com.badlogic.gdx.math.Vector3
import com.badlogic.gdx.utils.Array
import kotlin.random.Random

class DestroyedCity {

    private val modelBuilder = ModelBuilder()
    private val models = Array<Model>()
    private val buildings = Array<ModelInstance>()

    private val random = Random(1642)

    private val groundMaterial = Material(
        ColorAttribute.createDiffuse(Color(0.12f, 0.12f, 0.12f, 1f))
    )

    private val roadMaterial = Material(
        ColorAttribute.createDiffuse(Color(0.06f, 0.06f, 0.06f, 1f))
    )

    private val buildingMaterial = Material(
        ColorAttribute.createDiffuse(Color(0.32f, 0.30f, 0.27f, 1f))
    )

    private val rubbleMaterial = Material(
        ColorAttribute.createDiffuse(Color(0.20f, 0.18f, 0.16f, 1f))
    )

    fun generate(
        citySize: Int = 16,
        blockSize: Float = 42f,
        roadWidth: Float = 10f,
        destruction: Float = 0.5f
    ) {
        clear()

        createGround(
            citySize * blockSize,
            citySize * blockSize
        )

        for (x in 0 until citySize) {
            for (z in 0 until citySize) {
                createBlock(
                    x = x,
                    z = z,
                    blockSize = blockSize,
                    roadWidth = roadWidth,
                    destruction = destruction
                )
            }
        }
    }

    private fun createGround(width: Float, depth: Float) {
        val model = modelBuilder.createBox(
            width,
            1f,
            depth,
            groundMaterial,
            VertexAttributes.Usage.Position or
                    VertexAttributes.Usage.Normal
        )

        models.add(model)

        val instance = ModelInstance(
            model,
            width / 2f - 21f,
            -0.5f,
            depth / 2f - 21f
        )

        buildings.add(instance)
    }

    private fun createBlock(
        x: Int,
        z: Int,
        blockSize: Float,
        roadWidth: Float,
        destruction: Float
    ) {
        val centerX = x * blockSize
        val centerZ = z * blockSize

        createRoad(centerX, centerZ, blockSize)

        val availableSize = blockSize - roadWidth
        val buildingCount = random.nextInt(1, 4)

        repeat(buildingCount) {
            val buildingX = centerX + randomFloat(
                -availableSize * 0.35f,
                availableSize * 0.35f
            )

            val buildingZ = centerZ + randomFloat(
                -availableSize * 0.35f,
                availableSize * 0.35f
            )

            val width = randomFloat(9f, 18f)
            val depth = randomFloat(9f, 18f)
            val floors = random.nextInt(2, 10)

            createBuilding(
                x = buildingX,
                z = buildingZ,
                width = width,
                depth = depth,
                floors = floors,
                destruction = destruction
            )
        }

        if (random.nextFloat() < 0.15f) {
            createCrater(centerX, centerZ)
        }
    }

    private fun createRoad(
        x: Float,
        z: Float,
        size: Float
    ) {
        val model = modelBuilder.createBox(
            size,
            0.12f,
            size,
            roadMaterial,
            VertexAttributes.Usage.Position or
                    VertexAttributes.Usage.Normal
        )

        models.add(model)

        buildings.add(
            ModelInstance(
                model,
                x,
                -0.05f,
                z
            )
        )
    }

    private fun createBuilding(
        x: Float,
        z: Float,
        width: Float,
        depth: Float,
        floors: Int,
        destruction: Float
    ) {
        val floorHeight = 3.2f

        for (floor in 0 until floors) {
            val floorDestruction =
                destruction * (0.45f + floor.toFloat() / floors)

            if (random.nextFloat() < floorDestruction) {
                createRubble(
                    x = x,
                    z = z,
                    width = width,
                    depth = depth,
                    y = floor * floorHeight
                )
                continue
            }

            val height = floorHeight * 0.85f

            val model = modelBuilder.createBox(
                width,
                height,
                depth,
                buildingMaterial,
                VertexAttributes.Usage.Position or
                        VertexAttributes.Usage.Normal
            )

            models.add(model)

            val instance = ModelInstance(
                model,
                x,
                floor * floorHeight + height / 2f,
                z
            )

            instance.transform.rotate(
                Vector3.Y,
                randomFloat(-5f, 5f)
            )

            buildings.add(instance)

            if (random.nextFloat() < destruction) {
                createBrokenWall(
                    x = x,
                    z = z,
                    width = width,
                    depth = depth,
                    y = floor * floorHeight + 1.2f
                )
            }
        }

        createRubble(
            x = x,
            z = z,
            width = width,
            depth = depth,
            y = 0.2f
        )
    }

    private fun createBrokenWall(
        x: Float,
        z: Float,
        width: Float,
        depth: Float,
        y: Float
    ) {
        val wallWidth = randomFloat(2f, width * 0.7f)
        val wallHeight = randomFloat(1f, 2.5f)

        val model = modelBuilder.createBox(
            wallWidth,
            wallHeight,
            0.5f,
            buildingMaterial,
            VertexAttributes.Usage.Position or
                    VertexAttributes.Usage.Normal
        )

        models.add(model)

        val instance = ModelInstance(
            model,
            x + randomFloat(-width / 3f, width / 3f),
            y,
            z + depth / 2f
        )

        instance.transform.rotate(
            Vector3.Z,
            randomFloat(-25f, 25f)
        )

        buildings.add(instance)
    }

    private fun createRubble(
        x: Float,
        z: Float,
        width: Float,
        depth: Float,
        y: Float
    ) {
        repeat(random.nextInt(5, 15)) {
            val sizeX = randomFloat(0.3f, 1.5f)
            val sizeY = randomFloat(0.2f, 1.2f)
            val sizeZ = randomFloat(0.3f, 1.5f)

            val model = modelBuilder.createBox(
                sizeX,
                sizeY,
                sizeZ,
                rubbleMaterial,
                VertexAttributes.Usage.Position or
                        VertexAttributes.Usage.Normal
            )

            models.add(model)

            val instance = ModelInstance(
                model,
                x + randomFloat(-width / 1.5f, width / 1.5f),
                y + randomFloat(0f, 0.8f),
                z + randomFloat(-depth / 1.5f, depth / 1.5f)
            )

            instance.transform.rotate(
                Vector3.X,
                randomFloat(0f, 360f)
            )

            instance.transform.rotate(
                Vector3.Y,
                randomFloat(0f, 360f)
            )

            instance.transform.rotate(
                Vector3.Z,
                randomFloat(0f, 360f)
            )

            buildings.add(instance)
        }
    }

    private fun createCrater(x: Float, z: Float) {
        val model = modelBuilder.createCylinder(
            9f,
            0.15f,
            9f,
            32,
            rubbleMaterial,
            VertexAttributes.Usage.Position or
                    VertexAttributes.Usage.Normal
        )

        models.add(model)

        buildings.add(
            ModelInstance(
                model,
                x + randomFloat(-10f, 10f),
                0.05f,
                z + randomFloat(-10f, 10f)
            )
        )
    }

    fun render(
        modelBatch: ModelBatch,
        camera: PerspectiveCamera
    ) {
        modelBatch.begin(camera)

        for (instance in buildings) {
            modelBatch.render(instance)
        }

        modelBatch.end()
    }

    fun clear() {
        for (model in models) {
            model.dispose()
        }

        models.clear()
        buildings.clear()
    }

    private fun randomFloat(
        min: Float,
        max: Float
    ): Float {
        return min + random.nextFloat() * (max - min)
    }
}