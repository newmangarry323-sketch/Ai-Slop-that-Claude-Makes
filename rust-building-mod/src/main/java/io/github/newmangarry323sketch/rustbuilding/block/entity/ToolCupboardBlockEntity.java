package io.github.newmangarry323sketch.rustbuilding.block.entity;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.UUID;

import net.minecraft.core.BlockPos;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.storage.ValueInput;
import net.minecraft.world.level.storage.ValueOutput;

import io.github.newmangarry323sketch.rustbuilding.lock.CodeLock;
import io.github.newmangarry323sketch.rustbuilding.lock.LockHolder;
import io.github.newmangarry323sketch.rustbuilding.lock.Member;
import io.github.newmangarry323sketch.rustbuilding.registry.ModBlockEntities;

/**
 * Holds the building privilege list. As in Rust, anyone who can reach the cupboard can add themselves
 * or clear the list, which is why you lock it.
 */
public class ToolCupboardBlockEntity extends SyncedBlockEntity implements LockHolder {
	private final List<Member> authorized = new ArrayList<>();
	private final CodeLock lock = new CodeLock();

	public ToolCupboardBlockEntity(BlockPos pos, BlockState state) {
		super(ModBlockEntities.TOOL_CUPBOARD, pos, state);
	}

	public List<Member> authorized() {
		return Collections.unmodifiableList(this.authorized);
	}

	public boolean isAuthorized(UUID id) {
		for (Member member : this.authorized) {
			if (member.id().equals(id)) {
				return true;
			}
		}

		return false;
	}

	public void authorize(Player player) {
		if (!this.isAuthorized(player.getUUID())) {
			this.authorized.add(Member.of(player));
			this.sync();
		}
	}

	public void deauthorize(Player player) {
		if (this.authorized.removeIf(member -> member.id().equals(player.getUUID()))) {
			this.sync();
		}
	}

	public void clearAuthorized() {
		this.authorized.clear();
		this.sync();
	}

	@Override
	public CodeLock lock() {
		return this.lock;
	}

	@Override
	public void lockChanged() {
		this.sync();
	}

	@Override
	protected void saveAdditional(ValueOutput output) {
		super.saveAdditional(output);
		output.store("authorized", Member.CODEC.listOf(), List.copyOf(this.authorized));
		this.lock.save(output);
	}

	@Override
	protected void loadAdditional(ValueInput input) {
		super.loadAdditional(input);
		this.authorized.clear();
		this.authorized.addAll(input.read("authorized", Member.CODEC.listOf()).orElse(List.of()));
		this.lock.load(input);
	}
}
