package io.github.newmangarry323sketch.rustbuilding.lock;

import java.util.UUID;

import com.mojang.serialization.Codec;
import com.mojang.serialization.codecs.RecordCodecBuilder;

import net.minecraft.core.UUIDUtil;
import net.minecraft.world.entity.player.Player;

/** A player on an access list. The name is kept only so lists can be shown without a lookup. */
public record Member(UUID id, String name) {
	public static final Codec<Member> CODEC = RecordCodecBuilder.create(instance -> instance.group(
			UUIDUtil.CODEC.fieldOf("id").forGetter(Member::id),
			Codec.STRING.fieldOf("name").forGetter(Member::name)
	).apply(instance, Member::new));

	public static Member of(Player player) {
		return new Member(player.getUUID(), player.getName().getString());
	}
}
