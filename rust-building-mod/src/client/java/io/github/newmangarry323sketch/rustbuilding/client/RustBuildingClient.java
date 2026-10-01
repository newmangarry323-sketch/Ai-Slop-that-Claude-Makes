package io.github.newmangarry323sketch.rustbuilding.client;

import net.fabricmc.api.ClientModInitializer;

import io.github.newmangarry323sketch.rustbuilding.ClientBridge;

public final class RustBuildingClient implements ClientModInitializer {
	@Override
	public void onInitializeClient() {
		ClientBridge.install(new ClientBridgeImpl());
		PlacementPreview.init();
		BuildingHud.init();
	}
}
