#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UObject;

/**
 * Visual style ("look") of the run, for the 2D look tests (world3d.look, or -RPGLook= on the command line):
 *
 *   Mesh3D   mannequins with procedural poses, Starter Content materials (the original look)
 *   HD2D     pixel-art sprite cards standing in the lit 3D world, pixel-art textures on the terrain and
 *            houses, tree cards, tilt-shift depth of field (Octopath Traveler style)
 *   Flat2D   orthographic straight-down camera over a baked pixel-art map; flat sprites, y-sorted
 *
 * Gameplay is identical in every look: only what is drawn changes.
 */
namespace RPGLook
{
	enum class EMode : uint8 { Mesh3D, HD2D, Flat2D };

	/** Read once from data / the command line (ARPGGameMode, before the world is built). */
	void Init(const UObject* WorldContext);
	EMode Mode();
	inline bool IsSprite() { return Mode() != EMode::Mesh3D; }
	const TCHAR* ModeName();

	/** World units per sprite pixel. */
	float SpriteUnits();
	/** The fixed camera rotation of this look (cards are turned to face it). */
	FRotator CameraRotation();
	/** Rotation for a sprite card: the engine Plane turned to face the camera (HD-2D) or lying flat with
	 *  the picture's top to the north (Flat 2D). Card height runs along the returned rotation's -Y axis. */
	FRotator CardRotation();
	/** Flat 2D: card height above the ground for a world Y (south draws over north). */
	float FlatSortZ(float WorldY);

	/** HD-2D: the pixel-art replacement for a Starter Content material, or null to keep the original. */
	UMaterialInterface* PixelMaterial(const FString& StarterName);
	/** A pixel-art world material for a texture name ("grass", "rock", ...). */
	UMaterialInterface* PixelTexture(const FString& TexName);
	/** A new sprite material instance showing /Game/RPG/Pixel/<Texture> (Cols x Rows frames). */
	UMaterialInstanceDynamic* SpriteMaterial(UObject* Outer, const FString& Texture, int32 Cols = 1, int32 Rows = 1);
	/** A shared single-frame sprite material for props (trees, houses...). */
	UMaterialInterface* PropMaterial(const FString& Texture);
}
