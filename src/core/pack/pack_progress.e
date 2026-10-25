note
	description: "Immutable snapshot of successfully transferred data and completed entries."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class
	PACK_PROGRESS

create {ARCHIVE_PACK_SESSION}
	make

feature {NONE} -- Initialization

	make (a_name: detachable READABLE_STRING_32; entry_bytes, total_bytes, entries: NATURAL_64; finished: BOOLEAN)
			-- Snapshot progress independently of the session's mutable state.
		do
			if attached a_name as name then
				create entry_name.make_from_string (name)
			end

			entry_written_bytes := entry_bytes
			written_bytes := total_bytes
			completed_entries := entries
			is_finished := finished
		end

feature -- Access

	entry_name: detachable IMMUTABLE_STRING_32
			-- Current archive entry; absent at operation start and final success.

	entry_written_bytes: NATURAL_64
			-- Data bytes successfully passed to the archive writer for the current entry.

	written_bytes: NATURAL_64
			-- Total transferred data bytes, including represented sparse-file holes.

	completed_entries: NATURAL_64
			-- Entries whose native finish step succeeded.

	is_finished: BOOLEAN
			-- Have packing and resource cleanup completed successfully?

end
