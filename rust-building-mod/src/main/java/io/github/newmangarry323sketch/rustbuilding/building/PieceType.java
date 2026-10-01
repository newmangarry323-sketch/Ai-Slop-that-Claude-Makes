package io.github.newmangarry323sketch.rustbuilding.building;

import net.minecraft.network.chat.Component;

/**
 * The pieces the building plan can place. Wall-like pieces are described by which cells of their
 * 3 x 3 panel are solid: {@code u} runs along the wall, {@code h} is the height above the floor (1-3).
 */
public enum PieceType {
	FOUNDATION("foundation", Category.SLAB, null),
	FLOOR("floor", Category.SLAB, null),
	WALL("wall", Category.EDGE, new String[] {"###", "###", "###"}),
	DOORWAY("doorway", Category.EDGE, new String[] {"###", "#.#", "#.#"}),
	WINDOW("window", Category.EDGE, new String[] {"###", "#.#", "###"}),
	HALF_WALL("half_wall", Category.EDGE, new String[] {"...", "###", "###"}),
	LOW_WALL("low_wall", Category.EDGE, new String[] {"...", "...", "###"}),
	STAIRS("stairs", Category.STAIRS, null);

	public enum Category {
		SLAB,
		EDGE,
		STAIRS
	}

	private final String id;
	private final Category category;
	// Rows top (h = 3) to bottom (h = 1), columns u = 0..2.
	private final String[] pattern;

	PieceType(String id, Category category, String[] pattern) {
		this.id = id;
		this.category = category;
		this.pattern = pattern;
	}

	public String id() {
		return this.id;
	}

	public Category category() {
		return this.category;
	}

	/** Whether a wall-like piece has a block at column {@code u} (0-2), height {@code h} (1-3). */
	public boolean edgeHas(int u, int h) {
		if (this.pattern == null) {
			return false;
		}

		return this.pattern[3 - h].charAt(u) == '#';
	}

	public Component displayName() {
		return Component.translatable("piece.rustbuilding." + this.id);
	}

	public static PieceType byOrdinal(int ordinal) {
		PieceType[] values = values();
		return values[Math.floorMod(ordinal, values.length)];
	}
}
