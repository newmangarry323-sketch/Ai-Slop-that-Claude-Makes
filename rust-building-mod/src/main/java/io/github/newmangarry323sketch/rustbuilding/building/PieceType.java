package io.github.newmangarry323sketch.rustbuilding.building;

import java.util.List;

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
	STAIRS("stairs", Category.STAIRS, null),
	// Added after the rest so that building plans saved with a piece selected keep their selection.
	/** A beam across the top, posts at the ends and a 3 x 2 opening below: where a garage door goes. */
	WALL_FRAME("wall_frame", Category.EDGE, new String[] {"###", "...", "..."});

	/** The order the building plan's menu lists the pieces in. */
	public static final List<PieceType> MENU_ORDER = List.of(
			FOUNDATION, FLOOR, WALL, DOORWAY, WINDOW, WALL_FRAME, HALF_WALL, LOW_WALL, STAIRS);

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

	/**
	 * Whether the corner pillar at one end ({@code end} 0 or 1) of a wall-like piece reaches height
	 * {@code h}. Pillars run as high as the piece's end columns, except that a wall frame's posts run
	 * the full height beside its opening.
	 */
	public boolean needsPillar(int end, int h) {
		return this == WALL_FRAME || this.edgeHas(end * 2, h);
	}

	public Component displayName() {
		return Component.translatable("piece.rustbuilding." + this.id);
	}

	public static PieceType byOrdinal(int ordinal) {
		PieceType[] values = values();
		return values[Math.floorMod(ordinal, values.length)];
	}
}
