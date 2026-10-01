package io.github.newmangarry323sketch.rustbuilding;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import net.minecraft.resources.Identifier;

import net.fabricmc.api.ModInitializer;

import io.github.newmangarry323sketch.rustbuilding.network.ModNetworking;
import io.github.newmangarry323sketch.rustbuilding.registry.ModBlockEntities;
import io.github.newmangarry323sketch.rustbuilding.registry.ModBlocks;
import io.github.newmangarry323sketch.rustbuilding.registry.ModComponents;
import io.github.newmangarry323sketch.rustbuilding.registry.ModCreativeTab;
import io.github.newmangarry323sketch.rustbuilding.registry.ModItems;

/**
 * Rust-style building for Minecraft: a building plan that places whole foundations, walls and floors
 * on a fixed grid, a hammer that upgrades them through Rust's five grades, a tool cupboard that grants
 * building privilege, and code-locked doors.
 */
public final class RustBuilding implements ModInitializer {
	public static final String MOD_ID = "rustbuilding";
	public static final Logger LOGGER = LoggerFactory.getLogger(MOD_ID);

	@Override
	public void onInitialize() {
		ModComponents.init();
		ModBlocks.init();
		ModItems.init();
		ModBlockEntities.init();
		ModCreativeTab.init();
		ModNetworking.init();
		ProtectionEvents.init();
		LOGGER.info("Rust Building ready");
	}

	public static Identifier id(String path) {
		return Identifier.fromNamespaceAndPath(MOD_ID, path);
	}
}
