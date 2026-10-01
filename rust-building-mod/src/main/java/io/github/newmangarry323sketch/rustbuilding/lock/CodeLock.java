package io.github.newmangarry323sketch.rustbuilding.lock;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

import org.jspecify.annotations.Nullable;

import net.minecraft.core.UUIDUtil;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.level.storage.ValueInput;
import net.minecraft.world.level.storage.ValueOutput;

/**
 * A four-digit code lock, as fitted to a door or tool cupboard. Whoever enters the right code is
 * remembered, so they only type it once; changing the code forgets everyone but the owner.
 *
 * <p>The code itself never leaves the server: {@link #CODE_KEY} is stripped from the data sent to
 * clients, which only learn whether the lock is engaged and who may pass it.
 */
public final class CodeLock {
	public static final String KEY = "code_lock";
	public static final String CODE_KEY = "code";

	private boolean locked;
	private String code = "";
	@Nullable
	private UUID owner;
	private final List<UUID> whitelist = new ArrayList<>();

	public static boolean isValidCode(String code) {
		return code.length() == 4 && code.chars().allMatch(c -> c >= '0' && c <= '9');
	}

	public boolean isLocked() {
		return this.locked;
	}

	public boolean isOwner(Player player) {
		return this.owner != null && this.owner.equals(player.getUUID());
	}

	/** Whether this player may use what the lock guards right now. */
	public boolean canAccess(Player player) {
		return !this.locked || this.isOwner(player) || this.whitelist.contains(player.getUUID());
	}

	public void install(Player owner, String code) {
		this.locked = true;
		this.code = code;
		this.owner = owner.getUUID();
		this.whitelist.clear();
		this.whitelist.add(owner.getUUID());
	}

	/** Checks a guess; a right one adds the player to the list. */
	public boolean tryCode(Player player, String guess) {
		if (!this.locked || !this.code.equals(guess)) {
			return false;
		}

		if (!this.whitelist.contains(player.getUUID())) {
			this.whitelist.add(player.getUUID());
		}

		return true;
	}

	public void changeCode(String code) {
		this.code = code;
		this.whitelist.removeIf(id -> !id.equals(this.owner));
	}

	public void remove() {
		this.locked = false;
		this.code = "";
		this.owner = null;
		this.whitelist.clear();
	}

	public void save(ValueOutput output) {
		ValueOutput lock = output.child(KEY);
		lock.putBoolean("locked", this.locked);
		lock.putString(CODE_KEY, this.code);

		if (this.owner != null) {
			lock.store("owner", UUIDUtil.CODEC, this.owner);
		}

		lock.store("whitelist", UUIDUtil.CODEC.listOf(), List.copyOf(this.whitelist));
	}

	public void load(ValueInput input) {
		ValueInput lock = input.childOrEmpty(KEY);
		this.locked = lock.getBooleanOr("locked", false);
		this.code = lock.getStringOr(CODE_KEY, "");
		this.owner = lock.read("owner", UUIDUtil.CODEC).orElse(null);
		this.whitelist.clear();
		this.whitelist.addAll(lock.read("whitelist", UUIDUtil.CODEC.listOf()).orElse(List.of()));
	}
}
