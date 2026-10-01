package io.github.newmangarry323sketch.rustbuilding.registry;

import net.minecraft.core.Registry;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.registries.Registries;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.ResourceKey;
import net.minecraft.world.item.CreativeModeTab;
import net.minecraft.world.item.ItemStack;

import net.fabricmc.fabric.api.creativetab.v1.FabricCreativeModeTab;

import io.github.newmangarry323sketch.rustbuilding.RustBuilding;

public final class ModCreativeTab {
	public static final ResourceKey<CreativeModeTab> KEY = ResourceKey.create(Registries.CREATIVE_MODE_TAB, RustBuilding.id("main"));

	public static final CreativeModeTab TAB = FabricCreativeModeTab.builder()
			.icon(() -> new ItemStack(ModItems.BUILDING_PLAN))
			.title(Component.translatable("itemGroup.rustbuilding"))
			.displayItems((parameters, output) -> {
				output.accept(ModItems.BUILDING_PLAN);
				output.accept(ModItems.HAMMER);
				output.accept(ModItems.TOOL_CUPBOARD);
				output.accept(ModItems.CODE_LOCK);
				output.accept(ModItems.SHEET_METAL_DOOR);
				output.accept(ModItems.ARMORED_DOOR);
				output.accept(ModItems.GARAGE_DOOR);
			})
			.build();

	private ModCreativeTab() {
	}

	public static void init() {
		Registry.register(BuiltInRegistries.CREATIVE_MODE_TAB, KEY, TAB);
	}
}
