package io.github.newmangarry323sketch.rustbuilding.building;

import net.minecraft.util.StringRepresentable;

/**
 * What a single building block is doing inside the grid. Together with the block's position this is
 * enough to work out which piece it belongs to, so no block entity is needed per block.
 */
public enum BuildingKind implements StringRepresentable {
	/** Part of a foundation or floor: the slab level of a storey. */
	SLAB("slab"),
	/** Part of a wall-like piece, or a corner pillar between walls. */
	WALL("wall"),
	/** A leg filling the gap between a foundation and uneven ground below it. */
	SUPPORT("support");

	private final String serializedName;

	BuildingKind(String serializedName) {
		this.serializedName = serializedName;
	}

	@Override
	public String getSerializedName() {
		return this.serializedName;
	}
}
