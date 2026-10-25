note
	description: "Common archive path and latest operation result for readers and writers."
	author: "samedit66"
	email: "samedit66@yandex.ru"

deferred class
	ARCHIVE

feature {NONE} -- Initialization

	make_with_name (a_name: READABLE_STRING_GENERAL)
			-- Locate an archive by its filename or pathname.
		require
			name_not_empty: not a_name.is_empty
			name_without_nul: not a_name.has_code (0)
		do
			make_with_path (create {PATH}.make_from_string (a_name))
		end

	make_with_path (a_path: PATH)
			-- Resolve `a_path' now, independently of later working-directory changes.
		require
			path_not_empty: not a_path.is_empty
			path_without_nul: not a_path.name.has ('%U')
		do
			path := a_path.absolute_path
		end

feature -- Access

	path: PATH
			-- Absolute archive path. No file is opened by creation.

	libarchive_version: INTEGER_32
			-- Linked libarchive version: major * 1000000 + minor * 1000 + patch.
		external
			"C use <archive.h>"
		alias
			"archive_version_number"
		ensure
			instance_free: class
			positive_version: Result > 0
		end

feature -- Status report

	has_completed_operation: BOOLEAN
			-- Has the latest operation finished normally?

	is_successful: BOOLEAN
			-- Did the latest operation complete successfully?
		do
			Result := has_completed_operation and then error_snapshot = Void
		end

	last_error: ARCHIVE_ERROR
			-- Immutable error of the latest completed, unsuccessful operation.
		require
			operation_completed: has_completed_operation
			operation_failed: not is_successful
		do
			check attached error_snapshot as failure then
				Result := failure
			end
		end

feature {NONE} -- Result storage

	error_snapshot: detachable ARCHIVE_ERROR

invariant
	error_is_completed: error_snapshot /= Void implies has_completed_operation

end
