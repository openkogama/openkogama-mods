namespace OpenKogama.Mods.Patches;

public static class Features
{
    public static readonly (string Flag, string Type, string Method, int? Ret)[] Toggles =
    [
        ("no_build_limit", "ModelingBoxCountConstraint", "CanAddCubeAt", 1),
        ("no_build_limit", "ModelingBoxCountConstraint", "CanRemoveCubeAt", 1),
        ("no_build_limit", "ModelingDynamicBoxConstraint", "CanAddCubeAt", 1),
        ("no_build_limit", "ConstraintVisualizer", "Init", null),
        ("destructibles_unlock", "MVMaterial", "get_IsAvailable", 1),
        ("unlimited_muzzle_position", "Assets.Scripts.WorldObjectTypes.CustomGun.MVCustomGunBlueprint", "SetMuzzlePointPosition", null),

        ("atlas_check_skip", "MaterialLoader", "CheckAtlasIntegrity", 1),

        ("anticheat_off", "CheatHandling", "MachineBanDetected", null),
        ("anticheat_off", "CheatHandling", "CheatSoftwareRunningDetected", null),
        ("anticheat_off", "CheatHandling", "TextureHackDetected", null),
        ("anticheat_off", "CheatHandling", "ExecuteBan", null),
    ];

    public static readonly (string Event, string Type, string Method)[] Hooks =
    [
        ("world.object_added", "WorldNetwork", "AddWorldObject"),
        ("world.object_removed", "MVWorldObjectClientManagerNetwork", "DestroyWO"),
        ("world.object_transferred", "MVGroup", "TransferChild"),
        ("editor.move_character", "MVBuildModeAvatarLocal.EditMode", "MoveCharacter"),
        ("editor.add_prototype", "EditorWorldObjectCreation", "OnAddNewPrototype"),
        ("world.loaded", "WorldNetwork", "HandleDeserializedWorldData"),
        ("loading.start", "LoadingScreenHandler", "Start"),
        ("frame", "MVGameControllerBase", "Update"),
        ("frame.late", "FullScreenController", "LateUpdate"),
        ("app.quit", "MVGameControllerBase", "OnApplicationQuit"),
        ("editor.select", "SelectionController", "SelectWO"),
        ("editor.translate_start", "ESTranslate", "Enter"),
        ("editor.translate_end", "ESTranslate", "Exit"),
        ("editor.rotation_reset", "UGUI.Desktop.Scripts.EditMode.Gizmo.RotationHelper", "ResetRotation"),
        ("world.link_added", "MVNetworkGame.OperationRequests", "AddLink"),
        ("world.link_removed", "Links", "RemoveLink"),
        ("world.object_link_added", "MVNetworkGame.OperationRequests", "AddObjectLink"),
        ("world.object_link_removed", "ObjectLinks", "RemoveObjectLink"),
        ("editor.paint_cubes", "PaintCubes", "Execute"),
        ("editor.translate", "ESTranslate", "Execute"),
        ("editor.grid_point", "MVWorldObjectClient", "GetClosestGridPoint"),
        ("editor.rotate_step", "UGUI.Desktop.Scripts.EditMode.Gizmo.RotationHelper", "RotateStep"),
        ("camera.set", "MVCameraController", "SetCamera(CameraType)"),
        ("camera.blue_mode", "MainCameraManager", "set_BlueModeEnabled"),
        ("settings.slider_float", "SettingsSlider", "Initialize(string,float,float,float)"),
        ("settings.slider_int", "SettingsSlider", "Initialize(string,int,int,int)"),
        ("settings.slider_changed", "SettingsSlider", "ValueChanged"),
    ];

    public static readonly (string Flag, string Type, string Method, string Param, string Arg, double Fallback)[] Floats =
    [
        ("model_scale", "EditorWorldObjectCreation", "OnAddNewPrototype", "model_scale_value", "scale", 1.0),
        ("custom_grid", "MVWorldObjectClient", "GetClosestGridPoint", "custom_grid_size", "gridSize", 1.0),
    ];
}
