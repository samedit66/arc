note
	description: "Shared filename encoding and extraction-basename policy."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARCHIVE_FORMATS

inherit
	ARCHIVE_CONSTANTS

feature -- Filename encoding

	output_suffix (a_name: READABLE_STRING_GENERAL): STRING_32
			-- Longest supported creation suffix in lowercase; empty if unknown.
		local
			lower_name: STRING_32
		do
			create Result.make_empty
			lower_name := a_name.as_string_32.as_lower
			across <<".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst", ".tgz", ".tbz2", ".txz", ".tzst", ".tar", ".zip", ".7z">> as suffix loop
				if lower_name.ends_with (suffix) and suffix.count > Result.count then
					Result := suffix.to_string_32
				end
			end
		end

	format_for_suffix (suffix: READABLE_STRING_32): INTEGER
			-- Container for a recognized `output_suffix', or Unknown_format for an empty suffix.
		require
			known_suffix: suffix.is_empty or else output_suffix (suffix).same_string (suffix)
		do
			if suffix.is_empty then
				Result := Unknown_format
			elseif suffix.same_string (".zip") then
				Result := Zip
			elseif suffix.same_string (".7z") then
				Result := Seven_zip
			else
				Result := Tar
			end
		end

	compression_for_suffix (suffix: READABLE_STRING_32): INTEGER
			-- Outer compression for a recognized `output_suffix'; None for an empty suffix.
		require
			known_suffix: suffix.is_empty or else output_suffix (suffix).same_string (suffix)
		do
			if suffix.same_string (".tar.gz") or suffix.same_string (".tgz") then
				Result := Gzip
			elseif suffix.same_string (".tar.bz2") or suffix.same_string (".tbz2") then
				Result := Bzip2
			elseif suffix.same_string (".tar.xz") or suffix.same_string (".txz") then
				Result := Xz
			elseif suffix.same_string (".tar.zst") or suffix.same_string (".tzst") then
				Result := Zstd
			else
				Result := None
			end
		end

feature -- Extraction naming

	unpack_name (a_name: READABLE_STRING_32): STRING_32
			-- Fresh destination basename, stripping a known suffix or appending `.unpacked'.
		local
			lower_name, suffix: STRING_32
		do
			Result := a_name.to_string_32.twin
			lower_name := Result.as_lower
			suffix := output_suffix (lower_name)

			across <<".tar.lz4", ".rar", ".cpio", ".xar", ".iso">> as read_suffix loop
				if lower_name.ends_with (read_suffix) and read_suffix.count > suffix.count then
					suffix := read_suffix.to_string_32
				end
			end

			if not suffix.is_empty and Result.count > suffix.count then
				Result.keep_head (Result.count - suffix.count)
			else
				Result.append_string_general (".unpacked")
			end
		end

end
