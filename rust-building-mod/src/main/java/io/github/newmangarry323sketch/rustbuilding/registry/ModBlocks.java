package io.github.newmangarry323sketch.rustbuilding.registry;

import java.util.EnumMap;
import java.util.Map;
import java.util.function.Function;

import net.minecraft.core.Registry;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.registries.Registries;
import net.minecraft.resources.ResourceKey;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.SoundType;
import net.minecraft.world.level.block.state.BlockBehaviour;
import net.minecraft.world.level.block.state.properties.BlockSetType;
import net.minecraft.world.level.material.MapColor;
import net.minecraft.world.level.material.PushReaction;

import net.fabricmc.fabric.api.object.builder.v1.block.type.BlockSetTypeBuilder;
import net.fabricmc.fabric.api.registry.FlammableBlockRegistry;

import io.github.newmangarry323sketch.rustbuilding.RustBuilding;
import io.github.newmangarry323sketch.rustbuilding.block.BuildingBlock;
import io.github.newmangarry323sketch.rustbuilding.block.BuildingStairsBlock;
import io.github.newmangarry323sketch.rustbuilding.block.RustDoorBlock;
import io.github.newmangarry323sketch.rustbuilding.block.ToolCupboardBlock;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingTier;

public final class ModBlocks {
	/**
	 * Explosion resistance of every grade from wood up. High enough that a blast only reaches a building
	 * block from right beside it and is mostly stopped by it, so the piece's health - not this number -
	 * decides when it breaks. See {@link io.github.newmangarry323sketch.rustbuilding.raid.RaidDamage}.
	 */
	public static final float BUILDING_BLAST_RESISTANCE = 14.0F;

	private static final Map<BuildingTier, BuildingBlock> BUILDING = new EnumMap<>(BuildingTier.class);
	private static final Map<BuildingTier, BuildingStairsBlock> STAIRS = new EnumMap<>(BuildingTier.class);

	static {
		for (BuildingTier tier : BuildingTier.values()) {
			BuildingBlock block = register(tier.id(), properties -> new BuildingBlock(tier, properties), buildingProperties(tier));
			BUILDING.put(tier, block);
			STAIRS.put(tier, register(tier.id() + "_stairs",
					properties -> new BuildingStairsBlock(tier, block.defaultBlockState(), properties), buildingProperties(tier)));
		}
	}

	public static final ToolCupboardBlock TOOL_CUPBOARD = register("tool_cupboard", ToolCupboardBlock::new,
			BlockBehaviour.Properties.of()
					.mapColor(MapColor.WOOD)
					.sound(SoundType.WOOD)
					.strength(2.5F, 6.0F)
					.ignitedByLava());

	public static final BlockSetType SHEET_METAL_SET = BlockSetTypeBuilder.copyOf(BlockSetType.IRON)
			.openableByHand(true)
			.register(RustBuilding.id("sheet_metal"));

	public static final BlockSetType ARMORED_SET = BlockSetTypeBuilder.copyOf(BlockSetType.IRON)
			.openableByHand(true)
			.soundType(SoundType.NETHERITE_BLOCK)
			.register(RustBuilding.id("armored"));

	public static final RustDoorBlock SHEET_METAL_DOOR = register("sheet_metal_door",
			properties -> new RustDoorBlock(SHEET_METAL_SET, 250, properties), doorProperties(MapColor.METAL, SoundType.METAL));

	public static final RustDoorBlock ARMORED_DOOR = register("armored_door",
			properties -> new RustDoorBlock(ARMORED_SET, 800, properties), doorProperties(MapColor.COLOR_GRAY, SoundType.NETHERITE_BLOCK));

	private ModBlocks() {
	}

	public static BuildingBlock building(BuildingTier tier) {
		return BUILDING.get(tier);
	}

	public static BuildingStairsBlock stairs(BuildingTier tier) {
		return STAIRS.get(tier);
	}

	private static BlockBehaviour.Properties buildingProperties(BuildingTier tier) {
		BlockBehaviour.Properties properties = BlockBehaviour.Properties.of()
				.pushReaction(PushReaction.BLOCK)
				.noLootTable();

		return switch (tier) {
			// Twig: a lattice of sticks. Breaks by hand or to any blast, burns, and can be seen through.
			case TWIG -> properties
					.mapColor(MapColor.WOOD)
					.sound(SoundType.SCAFFOLDING)
					.strength(0.3F, 0.5F)
					.noOcclusion()
					.isViewBlocking((state, level, pos) -> false)
					.isSuffocating((state, level, pos) -> false)
					.isRedstoneConductor((state, level, pos) -> false)
					.ignitedByLava();
			// The rest cannot be mined; a destroy time of -1 is what bedrock uses.
			case WOOD -> properties
					.mapColor(MapColor.WOOD)
					.sound(SoundType.WOOD)
					.strength(-1.0F, BUILDING_BLAST_RESISTANCE)
					.ignitedByLava();
			case STONE -> properties
					.mapColor(MapColor.STONE)
					.sound(SoundType.STONE)
					.strength(-1.0F, BUILDING_BLAST_RESISTANCE);
			case METAL -> properties
					.mapColor(MapColor.METAL)
					.sound(SoundType.METAL)
					.strength(-1.0F, BUILDING_BLAST_RESISTANCE);
			case ARMORED -> properties
					.mapColor(MapColor.COLOR_GRAY)
					.sound(SoundType.NETHERITE_BLOCK)
					.strength(-1.0F, BUILDING_BLAST_RESISTANCE);
		};
	}

	private static BlockBehaviour.Properties doorProperties(MapColor color, SoundType sound) {
		return BlockBehaviour.Properties.of()
				.mapColor(color)
				.sound(sound)
				.strength(5.0F, BUILDING_BLAST_RESISTANCE)
				.noOcclusion()
				.pushReaction(PushReaction.BLOCK);
	}

	private static <T extends Block> T register(String name, Function<BlockBehaviour.Properties, T> factory, BlockBehaviour.Properties properties) {
		ResourceKey<Block> key = ResourceKey.create(Registries.BLOCK, RustBuilding.id(name));
		T block = factory.apply(properties.setId(key));
		return Registry.register(BuiltInRegistries.BLOCK, key, block);
	}

	public static void init() {
		// Twig and wood burn like planks do, so fire is a (slow) way into a wooden base, as in Rust.
		FlammableBlockRegistry flammable = FlammableBlockRegistry.getDefaultInstance();

		for (BuildingTier tier : new BuildingTier[] {BuildingTier.TWIG, BuildingTier.WOOD}) {
			flammable.add(building(tier), 5, 20);
			flammable.add(stairs(tier), 5, 20);
		}

		flammable.add(TOOL_CUPBOARD, 5, 20);
	}
}
