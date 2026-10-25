note
	description: "Read and extract an archive from disk."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARCHIVE_READER

inherit
	ARCHIVE

	ARCHIVE_FORMATS

create
	make_with_name,
	make_with_path

feature -- Access

	unpack_path: PATH
			-- Sibling directory named after the archive without its suffix.
		local
			name: STRING_32
		do
			if attached path.entry as entry then
				name := unpack_name (entry.name)
			else
				name := unpack_name ("archive")
			end

			Result := path.parent.extended (name)
		end

	new_stream: ARCHIVE_READ_STREAM
			-- Fresh unopened traversal with no password and status independent of this reader.
		do
			create Result.make (path)
		end

	new_stream_with_options (options: ARCHIVE_READ_OPTIONS): ARCHIVE_READ_STREAM
			-- Fresh unopened traversal capturing options without changing this reader's result.
		do
			create Result.make_with_options (path, options)
		end

feature -- Status report

	is_unpacking: BOOLEAN
			-- Is an extraction or its final notification in progress?

	is_verifying: BOOLEAN
			-- Is an integrity verification in progress?

	is_reading: BOOLEAN
			-- Is extraction or verification in progress on this reader?
		do
			Result := is_unpacking or is_verifying
		end

feature -- Operations

	unpack
			-- Extract files, directories and preserved links into `unpack_path'.
			-- Record an error if the destination exists or an entry cannot be restored.
			-- On failure, already extracted objects remain in the new directory.
			-- Hardlink targets must precede their links in the archive.
		require
			not_reading: not is_reading
		do
			unpack_with_options (create {UNPACK_OPTIONS}.make)
		end

	unpack_with_options (options: UNPACK_OPTIONS)
			-- Extract using the current options, frozen before the first notification.
			-- Observer exceptions propagate after cleanup; final observer failure preserves success.
		require
			not_reading: not is_reading
		local
			session: ARCHIVE_UNPACK_SESSION
		do
			is_unpacking := True
			has_completed_operation := False
			error_snapshot := Void
			create session.make (path, unpack_path, options)

			session.unpack

			error_snapshot := session.last_error
			has_completed_operation := True

			if is_successful then
				session.notify_finished
			end

			is_unpacking := False
		rescue
			is_unpacking := False
		end

	verify
			-- Read all entry contents without extracting, using no password.
			-- Success covers checks performed by libarchive and resource cleanup.
		require
			not_reading: not is_reading
		do
			verify_with_options (create {ARCHIVE_READ_OPTIONS}.make)
		end

	verify_with_options (options: ARCHIVE_READ_OPTIONS)
			-- Fully read the archive using captured options; publish success after cleanup.
			-- This does not validate extraction policy or provide authenticity guarantees.
		require
			not_reading: not is_reading
		local
			stream: detachable ARCHIVE_READ_STREAM
		do
			is_verifying := True
			has_completed_operation := False
			error_snapshot := Void
			stream := new_stream_with_options (options)

			stream.open
			from
			until
				stream.after
			loop
				stream.consume
				if not stream.after then
					stream.forth
				end
			end

			stream.close
			if stream.has_failed then
				error_snapshot := stream.last_error
			end
			has_completed_operation := True
			is_verifying := False
		rescue
			if attached stream as active_stream then
				active_stream.close
			end
			is_verifying := False
		end

end
