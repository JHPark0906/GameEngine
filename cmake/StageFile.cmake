# Puts one file beside a built executable, and puts it there whole.
#
# Run with `cmake -DSOURCE=<file> -DDESTINATION=<file> -P StageFile.cmake`.
#
# This exists instead of `cmake -E copy_if_different` because a copy writes into the destination in
# place: for as long as it takes, the file at that path is empty or half of the new one. Anything
# reading it then sees that. The engine's own tests read files staged beside an executable, the
# component schema among them, and a build running beside a test is not a rare accident here — it
# is the normal state of a machine with several worktrees on it.
#
# So the new contents go to a temporary name first and take the destination's place by rename.
# A rename either has happened or has not; there is no moment where the destination is half a file.
#
# The "if different" part is kept because staging runs on every build. Rewriting an unchanged file
# would move its timestamp forward for no reason, and timestamps are what the next build reads to
# decide what to do.
if(NOT DEFINED SOURCE OR NOT DEFINED DESTINATION)
    message(FATAL_ERROR "StageFile.cmake needs both SOURCE and DESTINATION.")
endif()

if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR "Nothing to stage: ${SOURCE} does not exist.")
endif()

set(needs_staging TRUE)
if(EXISTS "${DESTINATION}")
    file(SIZE "${SOURCE}" source_size)
    file(SIZE "${DESTINATION}" destination_size)
    if(source_size EQUAL destination_size)
        file(SHA256 "${SOURCE}" source_hash)
        file(SHA256 "${DESTINATION}" destination_hash)
        if(source_hash STREQUAL destination_hash)
            set(needs_staging FALSE)
        endif()
    endif()
endif()

if(needs_staging)
    # 임시 이름은 목적지 옆에 둔다. 다른 볼륨에 두면 이름 바꾸기가 복사로 풀려 원자성이 사라진다.
    set(staging_path "${DESTINATION}.staging")
    file(COPY_FILE "${SOURCE}" "${staging_path}" RESULT copy_failure)
    if(copy_failure)
        message(FATAL_ERROR "Could not stage ${SOURCE}: ${copy_failure}")
    endif()
    file(RENAME "${staging_path}" "${DESTINATION}" RESULT rename_failure)
    if(rename_failure)
        file(REMOVE "${staging_path}")
        message(FATAL_ERROR "Could not put ${DESTINATION} in place: ${rename_failure}")
    endif()
endif()
