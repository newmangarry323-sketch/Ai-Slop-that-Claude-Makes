package io.github.newmangarry323sketch.rustbuilding.registry;

import java.util.function.BiFunction;
import java.util.function.Function;

import net.minecraft.core.Registry;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.registries.Registries;
import net.minecraft.resources.ResourceKey;
import net.minecraft.world.item.BlockItem;
import net.minecraft.world.item.DoubleHighBlockItem;
import net.minecraft.world.item.Item;
import net.minecraft.world.level.block.Block;

import io.github.newmangarry323sketch.rustbuilding.RustBuilding;
import io.github.newmangarry323sketch.rustbuilding.item.BuildingPlanItem;
import io.github.newmangarry323sketch.rustbuilding.item.CodeLockItem;
import io.github.newmangarry323sketch.rustbuilding.item.HammerItem;

public final class ModItems {
	public static final Item BUILDING_PLAN = register("building_plan", BuildingPlanItem::new,
			new Item.Properties().stacksTo(1).component(ModComponents.SELECTED_PIECE, 0));

	public static final Item HAMMER = register("hammer", HammerItem::new, new Item.Properties().stacksTo(1));

	public static final Item CODE_LOCK = register("code_lock", CodeLockItem::new, new Item.Properties().stacksTo(16));

	public static final Item TOOL_CUPBOARD = registerBlockItem(ModBlocks.TOOL_CUPBOARD, BlockItem::new);

	public static final Item SHEET_METAL_DOOR = registerBlockItem(ModBlocks.SHEET_METAL_DOOR, DoubleHighBlockItem::new);

	public static final Item ARMORED_DOOR = registerBlockItem(ModBlocks.ARMORED_DOOR, DoubleHighBlockItem::new);

	private ModItems() {
	}

	private static Item register(String name, Function<Item.Properties, Item> factory, Item.Properties properties) {
		ResourceKey<Item> key = ResourceKey.create(Registries.ITEM, RustBuilding.id(name));
		Item item = factory.apply(properties.setId(key));
		return Registry.register(BuiltInRegistries.ITEM, key, item);
	}

	private static Item registerBlockItem(Block block, BiFunction<Block, Item.Properties, Item> factory) {
		ResourceKey<Item> key = ResourceKey.create(Registries.ITEM, BuiltInRegistries.BLOCK.getKey(block));
		Item item = factory.apply(block, new Item.Properties().useBlockDescriptionPrefix().setId(key));
		return Registry.register(BuiltInRegistries.ITEM, key, item);
	}

	public static void init() {
		// Loads the class, which registers the fields above.
	}
}
