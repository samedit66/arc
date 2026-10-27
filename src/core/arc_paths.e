note
	description: "Lexical path checks and explicit physical source classification."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_PATHS

feature -- Validation

	is_valid_path (path: PATH): BOOLEAN
			-- Is `path' nonempty and free of NUL, without inspecting the filesystem?
		do
			Result := not path.is_empty and not path.name.has ('%U')
		ensure
			instance_free: class
		end

	is_valid_entry_name (name: READABLE_STRING_32): BOOLEAN
			-- Is `name' nonempty and free of NUL, without imposing extraction pathname policy?
		do
			Result := not name.is_empty and not name.has ('%U')
		ensure
			instance_free: class
		end

feature -- Extraction names

	is_valid_extraction_name (name: READABLE_STRING_32; for_windows: BOOLEAN): BOOLEAN
			-- Is the original archive name relative and representable under the selected POSIX or Windows policy?
		local
			normalized: STRING_32
		do
			normalized := name.to_string_32.twin
			if for_windows then
				normalized.replace_substring_all ("\", "/")
			end
			Result := not normalized.is_empty and then normalized [1] /= '/' and then not normalized.has ('%U')
			across normalized.split ('/') as part loop
				Result := Result and not part.same_string ("..")
				if for_windows then
					Result := Result and is_valid_windows_component (part)
				end
			end
		ensure
			instance_free: class
		end

feature {NONE} -- Windows name policy

	is_valid_windows_component (part: READABLE_STRING_32): BOOLEAN
			-- Is a path component free of Windows devices, streams and normalization aliases?
		local
			base: STRING_32
			dot: INTEGER
		do
			Result := True
			if not part.is_empty and not part.same_string (".") then
				Result := part [part.count] /= '.' and part [part.count] /= ' '
				across part as character loop
					Result := Result and character.code >= 32 and not ({STRING_32} "<>:%"|?*").has (character)
				end
				base := part.to_string_32.as_upper
				dot := base.index_of ('.', 1)
				if dot > 0 then
					base.keep_head (dot - 1)
				end
				base.right_adjust
				Result := Result and not base.same_string ("CON") and not base.same_string ("PRN") and
					not base.same_string ("AUX") and not base.same_string ("NUL") and
					not base.same_string ("CONIN$") and not base.same_string ("CONOUT$")
				if base.count = 4 and then (base.starts_with ("COM") or base.starts_with ("LPT")) then
					Result := Result and not ({STRING_32} "123456789%/185/%/178/%/179/").has (base [4])
				end
			end
		ensure
			instance_free: class
		end

feature -- Source inspection

	classify_source (path: PATH)
			-- Inspect `path' physically without following links; retain type and system error.
		require
			valid_path: is_valid_path (path)
		local
			native: ARC_NATIVE
			name: NATIVE_STRING
		do
			create native.make
			name := path.native_string
			native.classify_source (name.item)
			source_type := native.last_source_type
			source_error_code := native.last_system_code
			source_cleanup_code := native.last_cleanup_code
		end

	source_cleanup_code: INTEGER
			-- Separate handle-close failure after a failed source classification, or zero.

	source_error_code: INTEGER
			-- Captured errno (positive) or Win32 code (negative); zero on successful classification.

	is_directory: BOOLEAN
			-- Was the latest inspected source a real directory?
		do
			Result := source_type = {ARC_NATIVE}.Directory_type
		end

	is_symbolic_link: BOOLEAN
			-- Was the latest inspected source a symbolic link, including a dangling link?
		do
			Result := source_type = {ARC_NATIVE}.Symbolic_link_type
		end

	is_regular: BOOLEAN
			-- Was the latest inspected source an ordinary file?
		do
			Result := source_type = {ARC_NATIVE}.Regular_type
		end

feature {NONE} -- Source snapshot

	source_type: INTEGER
			-- Native type from the latest classification; zero before inspection or for unsupported objects.

end
