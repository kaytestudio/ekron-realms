using System;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;

[InitializeOnLoad]
public static class CityPrototypeSetup
{
    private const string ScenePath = "Assets/Scenes/CityPreview.unity";
    static CityPrototypeSetup()
    {
        EditorApplication.delayCall += () =>
        {
            if (!Application.isBatchMode && !File.Exists(ScenePath)) CreatePreview();
        };
    }

    [MenuItem("City Prototype/Create preview (12 x 12)")]
    public static void CreatePreview() { Create(12, 7, .5f, .4f); }

    [MenuItem("City Prototype/Create large preview (32 x 32, heavy)")]
    public static void CreateLarge() { Create(32, 10, .55f, .45f); }

    private static Material Material(string name, Color color)
    {
        string path = "Assets/Materials/" + name + ".mat";
        var material = AssetDatabase.LoadAssetAtPath<Material>(path);
        if (material != null) return material;
        var shader = Shader.Find("Universal Render Pipeline/Lit");
        if (shader == null) throw new InvalidOperationException("URP shader unavailable; finish package import first.");
        material = new Material(shader) { color = color, enableInstancing = true };
        AssetDatabase.CreateAsset(material, path);
        return material;
    }

    private static void Create(int size, int floors, float destruction, float rubble)
    {
        Directory.CreateDirectory("Assets/Scenes");
        Directory.CreateDirectory("Assets/Materials");
        Directory.CreateDirectory("Assets/Settings"); AssetDatabase.Refresh();
        var pipeline = AssetDatabase.LoadAssetAtPath<UniversalRenderPipelineAsset>("Assets/Settings/CityURP.asset");
        if (pipeline == null)
        {
            var data = ScriptableObject.CreateInstance<UniversalRendererData>();
            AssetDatabase.CreateAsset(data, "Assets/Settings/CityRenderer.asset");
            pipeline = UniversalRenderPipelineAsset.Create(data);
            pipeline.renderScale = 1; pipeline.shadowDistance = 50;
            AssetDatabase.CreateAsset(pipeline, "Assets/Settings/CityURP.asset");
        }
        GraphicsSettings.defaultRenderPipeline = pipeline;
        QualitySettings.renderPipeline = pipeline;
        EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        var generator = new GameObject("CityGenerator").AddComponent<DestroyedCityGenerator>();
        generator.citySize = size; generator.blockSize = 42; generator.maxFloors = floors;
        generator.destruction = destruction; generator.rubbleAmount = rubble;
        generator.groundMaterial = Material("Ground_Material", new Color(.31f, .33f, .29f));
        generator.roadMaterial = Material("Road_Material", new Color(.16f, .17f, .18f));
        generator.buildingMaterial = Material("Building_Material", new Color(.59f, .59f, .56f));
        generator.rubbleMaterial = Material("Rubble_Material", new Color(.39f, .36f, .32f));
        // All other generator values, including seed/colliders, remain source defaults.
        var camera = new GameObject("FlyCamera").AddComponent<Camera>();
        camera.tag = "MainCamera"; camera.nearClipPlane = .15f; camera.farClipPlane = 2500;
        camera.transform.position = new Vector3((size - 1) * 21, 65, -45);
        camera.transform.rotation = Quaternion.Euler(18, 0, 0);
        camera.gameObject.AddComponent<UniversalAdditionalCameraData>();
        camera.gameObject.AddComponent<CityFlyCamera>();
        var light = new GameObject("Sun").AddComponent<Light>();
        light.type = LightType.Directional; light.intensity = 1.3f;
        light.transform.rotation = Quaternion.Euler(45, -30, 0);
        RenderSettings.ambientLight = new Color(.65f, .67f, .7f);
        PlayerSettings.companyName = "EkronPrototype";
        PlayerSettings.productName = "Destroyed City Preview";
        PlayerSettings.SetApplicationIdentifier(BuildTargetGroup.Android, "com.ekron.destroyedcitypreview");
        PlayerSettings.defaultInterfaceOrientation = UIOrientation.LandscapeLeft;
        PlayerSettings.colorSpace = ColorSpace.Linear;
        PlayerSettings.SetScriptingBackend(BuildTargetGroup.Android, ScriptingImplementation.IL2CPP);
        PlayerSettings.Android.targetArchitectures = AndroidArchitecture.ARM64;
        var settings = new SerializedObject(AssetDatabase.LoadAllAssetsAtPath("ProjectSettings/ProjectSettings.asset")[0]);
        var input = settings.FindProperty("activeInputHandler");
        if (input != null) { input.intValue = 1; settings.ApplyModifiedPropertiesWithoutUndo(); }
        EditorSceneManager.SaveScene(EditorSceneManager.GetActiveScene(), ScenePath);
        EditorBuildSettings.scenes = new[] { new EditorBuildSettingsScene(ScenePath, true) };
        AssetDatabase.SaveAssets();
    }

    // Unity -batchmode -projectPath PATH -executeMethod CityPrototypeSetup.BuildAndroid -quit -logFile build.log
    public static void BuildAndroid()
    {
        CreatePreview();
        Directory.CreateDirectory("Builds");
        EditorUserBuildSettings.androidBuildSubtarget = MobileTextureSubtarget.ASTC;
        var report = BuildPipeline.BuildPlayer(new BuildPlayerOptions {
            scenes = new[] { ScenePath }, locationPathName = "Builds/destroyed-city-preview.apk",
            target = BuildTarget.Android, options = BuildOptions.Development
        });
        if (report.summary.result != UnityEditor.Build.Reporting.BuildResult.Succeeded)
            throw new Exception("Android build failed: " + report.summary.result);
    }
}
