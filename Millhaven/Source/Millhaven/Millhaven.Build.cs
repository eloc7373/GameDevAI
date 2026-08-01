using UnrealBuildTool;

public class Millhaven : ModuleRules
{
	public Millhaven(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"ProceduralMeshComponent"
		});

		// UProceduralMeshComponent cooks collision at runtime (bCreateCollision
		// is true for the terrain section), which pulls in PhysicsCore types.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"PhysicsCore"
		});
	}
}
