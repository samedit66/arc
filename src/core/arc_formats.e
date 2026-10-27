note
	description: "Immutable string identifiers for the supported archive creation profiles."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_FORMATS

feature -- Creation profiles

	Tar: IMMUTABLE_STRING_8
			-- Uncompressed restricted PAX TAR.
		once
			create Result.make_from_string ("tar")
		ensure
			instance_free: class
		end

	Tar_gz: IMMUTABLE_STRING_8
			-- Restricted PAX TAR with gzip.
		once
			create Result.make_from_string ("tar.gz")
		ensure
			instance_free: class
		end

	Tar_bz2: IMMUTABLE_STRING_8
			-- Restricted PAX TAR with bzip2.
		once
			create Result.make_from_string ("tar.bz2")
		ensure
			instance_free: class
		end

	Tar_xz: IMMUTABLE_STRING_8
			-- Restricted PAX TAR with XZ.
		once
			create Result.make_from_string ("tar.xz")
		ensure
			instance_free: class
		end

	Tar_zstd: IMMUTABLE_STRING_8
			-- Restricted PAX TAR with Zstandard.
		once
			create Result.make_from_string ("tar.zst")
		ensure
			instance_free: class
		end

	Tar_lz4: IMMUTABLE_STRING_8
			-- Restricted PAX TAR with LZ4.
		once
			create Result.make_from_string ("tar.lz4")
		ensure
			instance_free: class
		end

	Zip: IMMUTABLE_STRING_8
			-- ZIP with DEFLATE.
		once
			create Result.make_from_string ("zip")
		ensure
			instance_free: class
		end

	Seven_zip: IMMUTABLE_STRING_8
			-- 7z using the libarchive default compression.
		once
			create Result.make_from_string ("7z")
		ensure
			instance_free: class
		end

	is_supported_format (value: READABLE_STRING_8): BOOLEAN
			-- Is `value' an exact supported profile name, independent of object identity?
		do
			across <<Tar, Tar_gz, Tar_bz2, Tar_xz, Tar_zstd, Tar_lz4, Zip, Seven_zip>> as profile loop
				Result := Result or else value.same_string (profile)
			end
		ensure
			instance_free: class
		end

feature -- Filename profiles

	output_suffix (name: READABLE_STRING_GENERAL): STRING_32
			-- Longest recognized creation suffix, lowercased; empty when unknown.
		local
			lower: STRING_32
		do
			create Result.make_empty
			lower := name.as_string_32.as_lower
			across <<".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst", ".tar.lz4", ".tgz", ".tbz2", ".txz", ".tzst", ".zip", ".7z", ".tar">> as suffix loop
				if lower.ends_with (suffix) and suffix.count > Result.count then
					Result := suffix.to_string_32
				end
			end
		ensure
			instance_free: class
		end

	profile_for_name (name: READABLE_STRING_GENERAL): STRING_8
			-- Supported string profile inferred from a suffix, or empty when unknown.
		local
			suffix: STRING_32
		do
			suffix := output_suffix (name)
			if suffix.same_string (".tgz") then
				Result := "tar.gz"
			elseif suffix.same_string (".tbz2") then
				Result := "tar.bz2"
			elseif suffix.same_string (".txz") then
				Result := "tar.xz"
			elseif suffix.same_string (".tzst") then
				Result := "tar.zst"
			elseif suffix.is_empty then
				create Result.make_empty
			else
				Result := suffix.substring (2, suffix.count).to_string_8
			end
		ensure
			instance_free: class
		end


end
