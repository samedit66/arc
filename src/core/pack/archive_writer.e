note
	description: "Register filesystem sources and create new archives with independent operation status."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class
	ARCHIVE_WRITER

inherit
	ARCHIVE
		redefine
			make_with_path
		end

create
	make_with_name,
	make_with_path

feature {NONE} -- Initialization

	make_with_path (a_path: PATH)
			-- Locate the output and initialize an empty source list without opening files.
		do
			Precursor (a_path)
			create sources.make (4)
		end

feature -- Registration

	add_path (a_path: PATH)
			-- Register `a_path' under its basename, preserving a directory root.
		require
			not_packing: not is_packing
			path_not_empty: not a_path.is_empty
			path_without_nul: not a_path.name.has ('%U')
			source_exists: source_exists (a_path)
			named_source: has_source_name (a_path)
		do
			check attached named_source_path (a_path).entry as name then
				add_path_as (a_path, name.name)
			end
		end

	add_path_as (a_path: PATH; an_entry_name: READABLE_STRING_GENERAL)
			-- Register a source at a nonempty relative archive name.
			-- Capture the path and name now; read contents only during packing.
		require
			not_packing: not is_packing
			path_not_empty: not a_path.is_empty
			path_without_nul: not a_path.name.has ('%U')
			source_exists: source_exists (a_path)
			valid_name: is_valid_entry_name (an_entry_name)
		local
			policy: ARCHIVE_PACK_NAMES
		do
			create policy
			sources.extend (create {ARCHIVE_PACK_SOURCE}.make (a_path, policy.normalized_name (an_entry_name)))
		end

	add_path_string (a_path: READABLE_STRING_GENERAL)
			-- Register a filesystem pathname string under its basename.
		require
			not_packing: not is_packing
			path_not_empty: not a_path.is_empty
			path_without_nul: not a_path.has_code (0)
			source_exists: source_exists (create {PATH}.make_from_string (a_path))
			named_source: has_source_name (create {PATH}.make_from_string (a_path))
		do
			add_path (create {PATH}.make_from_string (a_path))
		end

	add_path_string_as (a_path, an_entry_name: READABLE_STRING_GENERAL)
			-- Register a pathname string at an explicit archive name, copying both values.
		require
			not_packing: not is_packing
			path_not_empty: not a_path.is_empty
			path_without_nul: not a_path.has_code (0)
			source_exists: source_exists (create {PATH}.make_from_string (a_path))
			valid_name: is_valid_entry_name (an_entry_name)
		do
			add_path_as (create {PATH}.make_from_string (a_path), an_entry_name)
		end

	add_directory_contents (a_path: PATH)
			-- Register a real directory's contents at archive root, without its own entry.
			-- Capture the absolute path now; traverse and read contents only during packing.
		require
			not_packing: not is_packing
			path_not_empty: not a_path.is_empty
			path_without_nul: not a_path.name.has ('%U')
			source_is_directory: is_source_directory (a_path)
		do
			sources.extend (create {ARCHIVE_PACK_SOURCE}.make (a_path, ""))
		ensure
			one_source_added: source_count = old source_count + 1
		end

	add_directory_contents_string (a_path: READABLE_STRING_GENERAL)
			-- Register a real directory's contents from a pathname string, copying its value.
		require
			not_packing: not is_packing
			path_not_empty: not a_path.is_empty
			path_without_nul: not a_path.has_code (0)
			source_is_directory: is_source_directory (create {PATH}.make_from_string (a_path))
		do
			add_directory_contents (create {PATH}.make_from_string (a_path))
		ensure
			one_source_added: source_count = old source_count + 1
		end

feature -- Status report

	is_packing: BOOLEAN
			-- Is packing or its final notification in progress?

	source_count: INTEGER
			-- Number of registered source paths, retained after packing.
		do
			Result := sources.count
		end

	source_exists (a_path: PATH): BOOLEAN
			-- Does the filesystem object itself exist, including a dangling symbolic link?
		local
			info: FILE_INFO
		do
			create info.make
			info.set_is_following_symlinks (False)
			info.update (a_path.name)
			Result := info.exists
		end

	is_source_directory (a_path: PATH): BOOLEAN
			-- Is the filesystem object itself an existing directory, rather than a symbolic link?
		local
			info: FILE_INFO
		do
			create info.make
			info.set_is_following_symlinks (False)
			info.update (a_path.name)
			Result := info.exists and then info.is_directory
		end

	has_source_name (a_path: PATH): BOOLEAN
			-- Does `a_path' have a basename suitable for implicit archive naming?
		do
			Result := attached named_source_path (a_path).entry as name and then not name.is_empty and then is_valid_entry_name (name.name)
		end

	is_valid_entry_name (a_name: READABLE_STRING_GENERAL): BOOLEAN
			-- Is `a_name' a nonempty relative archive name with no NUL, backslash or parent component?
		local
			policy: ARCHIVE_PACK_NAMES
		do
			create policy
			Result := not a_name.is_empty and then policy.is_valid_name (a_name)
		end

feature -- Operations

	pack
			-- Create a new archive using its suffix, without password or notifications.
		require
			not_packing: not is_packing
			has_sources: source_count > 0
		do
			pack_with_options (create {PACK_OPTIONS}.make)
		end

	pack_with_options (options: PACK_OPTIONS)
			-- Create a new archive with captured options; retain sources on every outcome.
			-- Propagate observer exceptions after cleanup; final observer failure preserves success.
		require
			not_packing: not is_packing
			has_sources: source_count > 0
		local
			session: ARCHIVE_PACK_SESSION
		do
			is_packing := True
			has_completed_operation := False
			error_snapshot := Void
			create session.make (path, sources, options)

			session.pack

			error_snapshot := session.last_error
			has_completed_operation := True
			if is_successful then
				session.notify_finished
			end

			is_packing := False
		rescue
			is_packing := False
		end

feature {NONE} -- Storage and names

	named_source_path (a_path: PATH): PATH
		do
			Result := a_path.absolute_path
			from
			until
				not attached Result.entry as name or else not name.name.same_string_general (".")
			loop
				Result := Result.parent
			end
		end

	sources: ARRAYED_LIST [ARCHIVE_PACK_SOURCE]

end
