package io.github.newmangarry323sketch.rustbuilding.client.screen;

import java.util.ArrayList;
import java.util.List;

import net.minecraft.ChatFormatting;
import net.minecraft.client.Minecraft;
import net.minecraft.core.BlockPos;
import net.minecraft.network.chat.Component;
import net.minecraft.network.chat.MutableComponent;

import net.fabricmc.fabric.api.client.networking.v1.ClientPlayNetworking;

import io.github.newmangarry323sketch.rustbuilding.LockScreenMode;
import io.github.newmangarry323sketch.rustbuilding.block.entity.ToolCupboardBlockEntity;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingTier;
import io.github.newmangarry323sketch.rustbuilding.building.Costs;
import io.github.newmangarry323sketch.rustbuilding.lock.Member;
import io.github.newmangarry323sketch.rustbuilding.network.Payloads;
import io.github.newmangarry323sketch.rustbuilding.privilege.BuildingPrivilege;
import io.github.newmangarry323sketch.rustbuilding.upkeep.Upkeep;

/**
 * The cupboard's privilege list, with Rust's three actions (authorise, deauthorise, clear the list),
 * and its upkeep: what the base costs to keep up, how long the stored materials last, and the storage.
 */
public class ToolCupboardScreen extends MenuScreen {
	private static final int SHOWN_NAMES = 4;
	private final BlockPos pos;
	private List<Member> shown = List.of();
	private List<Component> shownUpkeep = List.of();

	public ToolCupboardScreen(BlockPos pos) {
		super(Component.translatable("block.rustbuilding.tool_cupboard"));
		this.pos = pos;
	}

	private ToolCupboardBlockEntity cupboard() {
		Minecraft minecraft = Minecraft.getInstance();
		return minecraft.level != null && minecraft.level.getBlockEntity(this.pos) instanceof ToolCupboardBlockEntity cupboard ? cupboard : null;
	}

	@Override
	protected boolean twoColumns() {
		return true;
	}

	@Override
	protected void build() {
		ToolCupboardBlockEntity cupboard = this.cupboard();
		Minecraft minecraft = Minecraft.getInstance();

		if (cupboard == null || minecraft.player == null || minecraft.level == null) {
			this.button(Component.translatable("gui.done"), button -> this.onClose());
			return;
		}

		this.shown = List.copyOf(cupboard.authorized());
		boolean authorized = cupboard.isAuthorized(minecraft.player.getUUID());
		this.lines().add(Component.translatable("screen.rustbuilding.cupboard.range", BuildingPrivilege.RANGE));
		this.lines().add(Component.translatable(authorized ? "screen.rustbuilding.cupboard.you_are" : "screen.rustbuilding.cupboard.you_are_not"));
		this.lines().add(Component.translatable("screen.rustbuilding.cupboard.list", this.shown.size()));

		for (int i = 0; i < Math.min(SHOWN_NAMES, this.shown.size()); i++) {
			this.lines().add(Component.literal(this.shown.get(i).name()));
		}

		if (this.shown.size() > SHOWN_NAMES) {
			this.lines().add(Component.translatable("screen.rustbuilding.cupboard.more", this.shown.size() - SHOWN_NAMES));
		}

		this.shownUpkeep = upkeepLines(cupboard, minecraft.level.getGameTime());
		this.lines().addAll(this.shownUpkeep);

		if (authorized) {
			this.button(Component.translatable("screen.rustbuilding.cupboard.deauthorize"), button -> this.send(Payloads.CupboardAction.DEAUTHORIZE));
		} else {
			this.button(Component.translatable("screen.rustbuilding.cupboard.authorize"), button -> this.send(Payloads.CupboardAction.AUTHORIZE));
		}

		this.button(Component.translatable("screen.rustbuilding.cupboard.clear"), button -> this.send(Payloads.CupboardAction.CLEAR));
		this.button(Component.translatable("screen.rustbuilding.cupboard.storage"), button -> this.send(Payloads.CupboardAction.OPEN_STORAGE));

		if (cupboard.lock().isLocked() && cupboard.lock().isOwner(minecraft.player)) {
			this.button(Component.translatable("screen.rustbuilding.lock.settings"),
					button -> minecraft.gui.setScreen(new CodeLockScreen(this.pos, LockScreenMode.OWNER)));
		}

		this.button(Component.translatable("gui.done"), button -> this.onClose());
	}

	/** "Upkeep for 12 pieces, every 24 days:", the materials, and how long they last. */
	private static List<Component> upkeepLines(ToolCupboardBlockEntity cupboard, long now) {
		List<Component> lines = new ArrayList<>();
		lines.add(Component.empty());

		if (cupboard.shownPieces() == 0) {
			lines.add(Component.translatable("screen.rustbuilding.cupboard.no_pieces"));
			return lines;
		}

		lines.add(Component.translatable("screen.rustbuilding.cupboard.upkeep", cupboard.shownPieces(), Upkeep.PERIOD / Upkeep.RUST_HOUR));
		MutableComponent costs = Component.empty();
		long protectedFor = Long.MAX_VALUE;
		List<Component> decaying = new ArrayList<>();

		for (BuildingTier tier : BuildingTier.values()) {
			int cost = cupboard.shownCost(tier);

			if (cost <= 0) {
				continue;
			}

			if (!costs.getSiblings().isEmpty()) {
				costs.append(Component.literal(", "));
			}

			costs.append(Costs.describe(tier, cost));
			long left = cupboard.shownCoveredUntil(tier) - now;

			if (left <= 0) {
				decaying.add(tier.displayName());
			}

			protectedFor = Math.min(protectedFor, left);
		}

		lines.add(costs);

		if (!decaying.isEmpty()) {
			MutableComponent grades = Component.empty();

			for (Component grade : decaying) {
				if (!grades.getSiblings().isEmpty()) {
					grades.append(Component.literal(", "));
				}

				grades.append(grade);
			}

			lines.add(Component.translatable("screen.rustbuilding.cupboard.decaying", grades).withStyle(ChatFormatting.RED));
		} else if (protectedFor != Long.MAX_VALUE) {
			lines.add(Component.translatable("screen.rustbuilding.cupboard.protected", duration(protectedFor)).withStyle(ChatFormatting.GREEN));
		}

		return lines;
	}

	/** Minecraft days, or hours (a thousand ticks) when less than a day is left. */
	private static Component duration(long ticks) {
		long days = ticks / Upkeep.RUST_HOUR;

		if (days >= 1) {
			return Component.translatable(days == 1 ? "screen.rustbuilding.cupboard.day" : "screen.rustbuilding.cupboard.days", days);
		}

		long hours = ticks / 1000;
		return hours >= 1
				? Component.translatable(hours == 1 ? "screen.rustbuilding.cupboard.hour" : "screen.rustbuilding.cupboard.hours", hours)
				: Component.translatable("screen.rustbuilding.cupboard.under_an_hour");
	}

	private void send(int action) {
		ClientPlayNetworking.send(new Payloads.CupboardAction(this.pos, action));
	}

	@Override
	public void tick() {
		super.tick();
		ToolCupboardBlockEntity cupboard = this.cupboard();

		if (cupboard == null) {
			this.onClose();
			return;
		}

		// The server's answers arrive as block entity updates; redraw when what is shown changes, which
		// includes the time left going down.
		Minecraft minecraft = Minecraft.getInstance();

		if (!cupboard.authorized().equals(this.shown)
				|| minecraft.level != null && !upkeepLines(cupboard, minecraft.level.getGameTime()).equals(this.shownUpkeep)) {
			this.refresh();
		}
	}
}
