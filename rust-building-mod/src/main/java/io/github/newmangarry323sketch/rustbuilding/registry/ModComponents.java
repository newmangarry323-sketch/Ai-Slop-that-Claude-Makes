package io.github.newmangarry323sketch.rustbuilding.registry;

import com.mojang.serialization.Codec;

import net.minecraft.core.Registry;
import net.minecraft.core.component.DataComponentType;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.network.codec.ByteBufCodecs;

import io.github.newmangarry323sketch.rustbuilding.RustBuilding;

public final class ModComponents {
	/** The piece a building plan places, as a {@link io.github.newmangarry323sketch.rustbuilding.building.PieceType} ordinal. */
	public static final DataComponentType<Integer> SELECTED_PIECE = Registry.register(
			BuiltInRegistries.DATA_COMPONENT_TYPE,
			RustBuilding.id("selected_piece"),
			DataComponentType.<Integer>builder().persistent(Codec.INT).networkSynchronized(ByteBufCodecs.VAR_INT).build()
	);

	private ModComponents() {
	}

	public static void init() {
		// Loads the class, which registers the fields above.
	}
}
