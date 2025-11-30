using UnrealBuildTool;

public class BVHImporter : ModuleRules
{
	public BVHImporter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"AnimationCore",
				"MeshDescription",
				"StaticMeshDescription",
				"SkeletalMeshDescription",
				"MeshUtilities",
				"AssetRegistry",
				"AnimationBlueprintLibrary",
				"InterchangeCore",
				"InterchangeEngine",
				"InterchangeFactoryNodes",
				"InterchangeImport",
				"InterchangeCommonParser",
				"InterchangeNodes",
				"MeshConversion"
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
