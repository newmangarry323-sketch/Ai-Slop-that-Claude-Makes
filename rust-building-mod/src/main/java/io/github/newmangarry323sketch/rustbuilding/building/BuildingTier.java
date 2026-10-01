package io.github.newmangarry323sketch.rustbuilding.building;

import org.jspecify.annotations.Nullable;

import net.minecraft.network.chat.Component;
import net.minecraft.tags.ItemTags;
import net.minecraft.tags.TagKey;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;

/**
 * Rust's building grades. Health values are Rust's own (twig 10, wood 250, stone 500, sheet metal 1000,
 * armored 2000); the materials are Minecraft stand-ins for Rust's wood, stone, metal fragments and
 * high quality metal.
 */
public enum BuildingTier {
	TWIG("twig", 10, Items.STICK, null, 1),
	WOOD("wood", 250, Items.OAK_PLANKS, ItemTags.PLANKS, 1),
	STONE("stone", 500, Items.COBBLESTONE, ItemTags.STONE_CRAFTING_MATERIALS, 1),
	METAL("sheet_metal", 1000, Items.IRON_NUGGET, null, 2),
	ARMORED("armored", 2000, Items.IRON_INGOT, null, 1);

	private final String id;
	private final int health;
	private final Item material;
	@Nullable
	private final TagKey<Item> materialTag;
	private final int perUnit;

	BuildingTier(String id, int health, Item material, @Nullable TagKey<Item> materialTag, int perUnit) {
		this.id = id;
		this.health = health;
		this.material = material;
		this.materialTag = materialTag;
		this.perUnit = perUnit;
	}

	public String id() {
		return this.id;
	}

	/** Hit points of one piece of this grade, as in Rust. */
	public int health() {
		return this.health;
	}

	/** The item handed back on refunds, and shown in cost hints. */
	public Item material() {
		return this.material;
	}

	public boolean accepts(ItemStack stack) {
		return this.materialTag != null ? stack.is(this.materialTag) : stack.is(this.material);
	}

	/** Materials needed for a piece of the given size, in {@link #material()} units. */
	public int cost(int units) {
		return units * this.perUnit;
	}

	public Component displayName() {
		return Component.translatable("tier.rustbuilding." + this.id);
	}

	public Component materialName() {
		return Component.translatable("material.rustbuilding." + this.id);
	}

	@Nullable
	public BuildingTier next() {
		int next = this.ordinal() + 1;
		return next < values().length ? values()[next] : null;
	}

	public static BuildingTier byOrdinal(int ordinal) {
		BuildingTier[] values = values();
		return values[Math.floorMod(ordinal, values.length)];
	}
}
