package io.github.newmangarry323sketch.rustbuilding.building;

import net.minecraft.network.chat.Component;
import net.minecraft.world.entity.player.Inventory;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.ItemStack;

/** Paying for pieces out of the player's inventory. Creative players pay nothing. */
public final class Costs {
	private Costs() {
	}

	public static int count(Player player, BuildingTier tier) {
		Inventory inventory = player.getInventory();
		int total = 0;

		for (int slot = 0; slot < inventory.getContainerSize(); slot++) {
			ItemStack stack = inventory.getItem(slot);

			if (!stack.isEmpty() && tier.accepts(stack)) {
				total += stack.getCount();
			}
		}

		return total;
	}

	public static boolean canAfford(Player player, BuildingTier tier, int amount) {
		return player.hasInfiniteMaterials() || count(player, tier) >= amount;
	}

	/** Takes the materials if the player has them all; takes nothing otherwise. */
	public static boolean take(Player player, BuildingTier tier, int amount) {
		if (player.hasInfiniteMaterials()) {
			return true;
		}

		if (count(player, tier) < amount) {
			return false;
		}

		Inventory inventory = player.getInventory();
		int remaining = amount;

		for (int slot = 0; slot < inventory.getContainerSize() && remaining > 0; slot++) {
			ItemStack stack = inventory.getItem(slot);

			if (!stack.isEmpty() && tier.accepts(stack)) {
				int taken = Math.min(remaining, stack.getCount());
				stack.shrink(taken);
				remaining -= taken;
			}
		}

		inventory.setChanged();
		return true;
	}

	/** Hands materials back, dropping what does not fit. */
	public static void give(Player player, BuildingTier tier, int amount) {
		if (player.hasInfiniteMaterials()) {
			return;
		}

		int remaining = amount;

		while (remaining > 0) {
			int count = Math.min(remaining, 64);
			player.getInventory().placeItemBackInInventory(new ItemStack(tier.material(), count));
			remaining -= count;
		}
	}

	/** "9 Sticks", "18 Iron Nuggets"... */
	public static Component describe(BuildingTier tier, int amount) {
		return Component.translatable("cost.rustbuilding.amount", amount, tier.materialName());
	}
}
