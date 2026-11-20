using System.Collections;
using UnityEngine;

public class DestroyedCityGenerator : MonoBehaviour
{
    [Header("Размер города")]
    [Min(4)]
    public int citySize = 24;

    [Min(8)]
    public float blockSize = 42f;

    [Min(4)]
    public float roadWidth = 10f;

    [Header("Здания")]
    [Min(1)]
    public int minFloors = 2;

    [Min(2)]
    public int maxFloors = 10;

    [Min(4)]
    public float floorHeight = 3.2f;

    [Min(4)]
    public float buildingMinSize = 9f;

    [Min(6)]
    public float buildingMaxSize = 19f;

    [Range(0f, 1f)]
    public float destruction = 0.48f;

    [Header("Завалы")]
    [Range(0f, 1f)]
    public float rubbleAmount = 0.65f;

    [Min(0)]
    public int rubblePerBuilding = 18;

    [Header("Воронки")]
    [Range(0f, 1f)]
    public float craterChance = 0.12f;

    [Min(0.2f)]
    public float craterRadius = 5f;

    [Header("Производительность")]
    [Tooltip("После скольких кварталов делать паузу на кадр")]
    [Min(1)]
    public int blocksPerFrame = 2;

    [Header("Генерация")]
    public int seed = 1642;
    public bool generateOnStart = true;
    public bool addColliders = true;

    [Header("Материалы")]
    public Material roadMaterial;
    public Material buildingMaterial;
    public Material rubbleMaterial;
    public Material groundMaterial;

    private Transform cityRoot;
    private System.Random random;
    private int generatedBlocks;

    private void Start()
    {
        if (generateOnStart)
        {
            StartCoroutine(GenerateCity());
        }
    }

    [ContextMenu("Generate City")]
    public void GenerateFromEditor()
    {
        StopAllCoroutines();
        ClearCity();
        StartCoroutine(GenerateCity());
    }

    [ContextMenu("Clear City")]
    public void ClearFromEditor()
    {
        StopAllCoroutines();
        ClearCity();
    }

    private IEnumerator GenerateCity()
    {
        ClearCity();

        random = new System.Random(seed);
        generatedBlocks = 0;

        cityRoot = new GameObject("PROCEDURAL_DESTROYED_CITY").transform;
        cityRoot.SetParent(transform);

        CreateGround();

        for (int x = 0; x < citySize; x++)
        {
            for (int z = 0; z < citySize; z++)
            {
                CreateBlock(x, z);

                generatedBlocks++;

                if (generatedBlocks >= blocksPerFrame)
                {
                    generatedBlocks = 0;
                    yield return null;
                }
            }
        }

        Debug.Log("Разрушенный город создан. Seed: " + seed);
    }

    private void CreateGround()
    {
        float totalSize = citySize * blockSize;

        GameObject ground = GameObject.CreatePrimitive(PrimitiveType.Cube);
        ground.name = "Ground";
        ground.transform.SetParent(cityRoot);

        ground.transform.position = new Vector3(
            totalSize * 0.5f - blockSize * 0.5f,
            -0.65f,
            totalSize * 0.5f - blockSize * 0.5f
        );

        ground.transform.localScale = new Vector3(
            totalSize,
            1f,
            totalSize
        );

        ApplyMaterial(ground, groundMaterial);
    }

    private void CreateBlock(int blockX, int blockZ)
    {
        float centerX = blockX * blockSize;
        float centerZ = blockZ * blockSize;

        CreateRoadPatch(centerX, centerZ);

        float usableSize = blockSize - roadWidth;
        float buildingSize = RandomRange(buildingMinSize, buildingMaxSize);

        int buildingCount = RandomRangeInt(1, 4);

        for (int i = 0; i < buildingCount; i++)
        {
            float localX = RandomRange(
                -usableSize * 0.35f,
                usableSize * 0.35f
            );

            float localZ = RandomRange(
                -usableSize * 0.35f,
                usableSize * 0.35f
            );

            float width = RandomRange(
                buildingSize * 0.65f,
                buildingSize
            );

            float depth = RandomRange(
                buildingSize * 0.65f,
                buildingSize
            );

            float worldX = centerX + localX;
            float worldZ = centerZ + localZ;

            CreateDestroyedBuilding(
                worldX,
                worldZ,
                width,
                depth
            );
        }

        if (Random.value < craterChance)
        {
            CreateCrater(centerX, centerZ);
        }
    }

    private void CreateRoadPatch(float x, float z)
    {
        GameObject road = GameObject.CreatePrimitive(PrimitiveType.Cube);
        road.name = "Road";

        road.transform.SetParent(cityRoot);
        road.transform.position = new Vector3(x, -0.08f, z);
        road.transform.localScale = new Vector3(
            blockSize,
            0.15f,
            blockSize
        );

        ApplyMaterial(road, roadMaterial);
    }

    private void CreateDestroyedBuilding(
        float x,
        float z,
        float width,
        float depth
    )
    {
        int floors = RandomRangeInt(minFloors, maxFloors + 1);

        float rotationY = RandomRange(-5f, 5f);

        GameObject buildingRoot = new GameObject("Ruined_Building");
        buildingRoot.transform.SetParent(cityRoot);

        buildingRoot.transform.position = new Vector3(x, 0f, z);
        buildingRoot.transform.rotation = Quaternion.Euler(
            0f,
            rotationY,
            0f
        );

        for (int floor = 0; floor < floors; floor++)
        {
            float floorY = floor * floorHeight + floorHeight * 0.5f;

            // Чем выше этаж, тем выше шанс разрушения.
            float destructionChance =
                destruction * (0.45f + floor / (float)floors);

            if (Random.value < destructionChance)
            {
                CreateRubbleAround(
                    buildingRoot.transform,
                    width,
                    depth,
                    floorY
                );

                continue;
            }

            CreateFloor(
                buildingRoot.transform,
                width,
                depth,
                floorY,
                floor
            );

            // Иногда разрушаем часть фасада.
            if (Random.value < destruction)
            {
                CreateBrokenWall(
                    buildingRoot.transform,
                    width,
                    depth,
                    floorY
                );
            }
        }

        // Куски здания возле основания.
        if (Random.value < rubbleAmount)
        {
            CreateRubbleAround(
                buildingRoot.transform,
                width,
                depth,
                0.4f
            );
        }
    }

    private void CreateFloor(
        Transform parent,
        float width,
        float depth,
        float y,
        int floorIndex
    )
    {
        GameObject floor = GameObject.CreatePrimitive(
            PrimitiveType.Cube
        );

        floor.name = "Floor_" + floorIndex;
        floor.transform.SetParent(parent);

        floor.transform.localPosition = new Vector3(
            0f,
            y,
            0f
        );

        floor.transform.localScale = new Vector3(
            width,
            floorHeight * 0.88f,
            depth
        );

        ApplyMaterial(floor, buildingMaterial);

        // Случайно наклоняем повреждённые этажи.
        if (Random.value < destruction * 0.35f)
        {
            floor.transform.localRotation = Quaternion.Euler(
                RandomRange(-8f, 8f),
                RandomRange(-4f, 4f),
                RandomRange(-8f, 8f)
            );
        }
    }

    private void CreateBrokenWall(
        Transform parent,
        float width,
        float depth,
        float y
    )
    {
        int side = RandomRangeInt(0, 4);

        float wallThickness = RandomRange(0.3f, 0.8f);
        float wallHeight = RandomRange(1.2f, 2.5f);

        GameObject wall = GameObject.CreatePrimitive(
            PrimitiveType.Cube
        );

        wall.name = "Broken_Wall";
        wall.transform.SetParent(parent);

        Vector3 localPosition;
        Vector3 localScale;

        if (side == 0)
        {
            localPosition = new Vector3(
                0f,
                y + RandomRange(-0.4f, 0.5f),
                depth * 0.5f
            );

            localScale = new Vector3(
                width * RandomRange(0.3f, 0.8f),
                wallHeight,
                wallThickness
            );
        }
        else if (side == 1)
        {
            localPosition = new Vector3(
                0f,
                y + RandomRange(-0.4f, 0.5f),
                -depth * 0.5f
            );

            localScale = new Vector3(
                width * RandomRange(0.3f, 0.8f),
                wallHeight,
                wallThickness
            );
        }
        else if (side == 2)
        {
            localPosition = new Vector3(
                width * 0.5f,
                y + RandomRange(-0.4f, 0.5f),
                0f
            );

            localScale = new Vector3(
                wallThickness,
                wallHeight,
                depth * RandomRange(0.3f, 0.8f)
            );
        }
        else
        {
            localPosition = new Vector3(
                -width * 0.5f,
                y + RandomRange(-0.4f, 0.5f),
                0f
            );

            localScale = new Vector3(
                wallThickness,
                wallHeight,
                depth * RandomRange(0.3f, 0.8f)
            );
        }

        wall.transform.localPosition = localPosition;
        wall.transform.localScale = localScale;

        wall.transform.localRotation = Quaternion.Euler(
            RandomRange(-15f, 15f),
            RandomRange(-15f, 15f),
            RandomRange(-15f, 15f)
        );

        ApplyMaterial(wall, buildingMaterial);
    }

    private void CreateRubbleAround(
        Transform parent,
        float width,
        float depth,
        float y
    )
    {
        int amount = RandomRangeInt(
            Mathf.Max(3, rubblePerBuilding / 2),
            rubblePerBuilding + 1
        );

        for (int i = 0; i < amount; i++)
        {
            GameObject rubble = GameObject.CreatePrimitive(
                PrimitiveType.Cube
            );

            rubble.name = "Rubble";
            rubble.transform.SetParent(parent);

            rubble.transform.localPosition = new Vector3(
                RandomRange(-width * 0.7f, width * 0.7f),
                y + RandomRange(0f, 1.2f),
                RandomRange(-depth * 0.7f, depth * 0.7f)
            );

            rubble.transform.localScale = new Vector3(
                RandomRange(0.25f, 1.5f),
                RandomRange(0.2f, 1.2f),
                RandomRange(0.25f, 1.5f)
            );

            rubble.transform.localRotation = Quaternion.Euler(
                RandomRange(0f, 360f),
                RandomRange(0f, 360f),
                RandomRange(0f, 360f)
            );

            ApplyMaterial(rubble, rubbleMaterial);
        }
    }

    private void CreateCrater(float x, float z)
    {
        GameObject crater = GameObject.CreatePrimitive(
            PrimitiveType.Cylinder
        );

        crater.name = "Crater";
        crater.transform.SetParent(cityRoot);

        float radius = RandomRange(
            craterRadius * 0.6f,
            craterRadius * 1.4f
        );

        crater.transform.position = new Vector3(
            x + RandomRange(-blockSize * 0.3f, blockSize * 0.3f),
            0.01f,
            z + RandomRange(-blockSize * 0.3f, blockSize * 0.3f)
        );

        crater.transform.localScale = new Vector3(
            radius,
            0.04f,
            radius
        );

        ApplyMaterial(crater, rubbleMaterial);
    }

    private void ApplyMaterial(
        GameObject target,
        Material material
    )
    {
        Renderer renderer = target.GetComponent<Renderer>();

        if (renderer != null && material != null)
        {
            renderer.sharedMaterial = material;
        }

        if (!addColliders)
        {
            Collider collider = target.GetComponent<Collider>();

            if (collider != null)
            {
                Destroy(collider);
            }
        }
    }

    private float RandomRange(float min, float max)
    {
        return (float)(min + random.NextDouble() * (max - min));
    }

    private int RandomRangeInt(int min, int max)
    {
        return random.Next(min, max);
    }

    private void ClearCity()
    {
        Transform oldCity = transform.Find(
            "PROCEDURAL_DESTROYED_CITY"
        );

        if (oldCity == null)
        {
            return;
        }

        if (Application.isPlaying)
        {
            Destroy(oldCity.gameObject);
        }
        else
        {
            DestroyImmediate(oldCity.gameObject);
        }
    }
}