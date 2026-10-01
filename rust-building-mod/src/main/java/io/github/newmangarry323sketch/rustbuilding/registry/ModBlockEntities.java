package io.github.newmangarry323sketch.rustbuilding.registry;

import net.minecraft.core.Registry;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.entity.BlockEntity;
import net.minecraft.world.level.block.entity.BlockEntityType;

import net.fabricmc.fabric.api.object.builder.v1.block.entity.FabricBlockEntityTypeBuilder;

import io.github.newmangarry323sketch.rustbuilding.RustBuilding;
import io.github.newmangarry323sketch.rustbuilding.block.entity.RustDoorBlockEntity;
import io.github.newmangarry323sketch.rustbuilding.block.entity.ToolCupboardBlockEntity;

public final class ModBlockEntities {
	public static final BlockEntityType<ToolCupboardBlockEntity> TOOL_CUPBOARD =
			register("tool_cupboard", ToolCupboardBlockEntity::new, ModBlocks.TOOL_CUPBOARD);

	public static final BlockEntityType<RustDoorBlockEntity> RUST_DOOR =
			register("rust_door", RustDoorBlockEntity::new, ModBlocks.SHEET_METAL_DOOR, ModBlocks.ARMORED_DOOR);

	private ModBlockEntities() {
	}

	private static <T extends BlockEntity> BlockEntityType<T> register(String name, FabricBlockEntityTypeBuilder.Factory<? extends T> factory, Block... blocks) {
		return Registry.register(BuiltInRegistries.BLOCK_ENTITY_TYPE, RustBuilding.id(name),
				FabricBlockEntityTypeBuilder.<T>create(factory, blocks).build());
	}

	public static void init() {
		// Loads the class, which registers the fields above.
	}
}
