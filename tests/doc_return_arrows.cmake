# Temporary N1 bridge for frozen v0 documents, not a general source converter.
# Only these reviewed declaration headers are adapted. Remove with the v0 audits.
include_guard(GLOBAL)
function(doc_return_arrows variable filename)
    if(filename STREQUAL "lang-collections")
        set(hash d9c77a85dae736ce64358c4c7c4980d6af52311a17b13d86d7e5b3ce772e912e)
        set(headers
            "findByName(collection: UserCollection, name: str):"
            "removeByName(collection: &UserCollection, name: str):"
            "next(cursor: &Cursor<T>):" "cursor(items: Self):"
            "where(items: Self, keep: Fn<(T): bool>):" "count(items: Self):")
    elseif(filename STREQUAL "lang-errors")
        set(hash f9c965f8586cb902d557d2fc4d836204f74f7a84b9d1db593c77c6aaf0951a09)
        set(headers
            "parse(text: str):" "load(text: str):" "greet(users: Vec<User>):"
            "close(file: &File):" "getAgeRange(user: User):" "readFile(path: str):"
            "loadJson(path: str):" "read(path: str):" "loadUser(path: str):"
            "get(url: str):" "fetch(source: str):" "validate(json: Json):"
            "open(path: str):" "loadOrEmpty(path: str):"
            "new(name: str, age: int):" "fromJson(json: Json):" "make():"
            "double(x: int):")
    elseif(filename STREQUAL "lang-ex-base")
        set(hash 6997278ee2a8593fc1e1fb278123a066551757b0ffc84cd4ba483e4965f6ddab)
        set(headers
            "add(x: int, y: int):" "log(message: str):" "getUser(value: Self):"
            "getName(value: Self):" "getAge(value: Self):" "getAgeRange(value: Self):"
            "getUser(user: User):" "new(name: str, age: int):" "fromJson(json: Json):"
            "toJson(user: User):" "getUser(admin: Admin):" "getName(admin: Admin):"
            "new():" "add(collection: &UserCollection, user: &User):"
            "toJson(collection: UserCollection):"
            "findByName(collection: UserCollection, name: str):"
            "removeByName(collection: &UserCollection, name: str):"
            "count(collection: UserCollection):"
            "olderThan(collection: UserCollection, age: int):")
    elseif(filename STREQUAL "lang-functions")
        set(hash e496a370d9fb073e51c24ec1771a6903ae11d04d13f33d9eccee108b351d66be)
        set(headers
            "add(x: int, y: int):" "validate(age: int):"
            "where(items: Self, keep: Fn<(T): bool>):" "loadUser(path: str):"
            "double(x: int):" "describe(age: int):" "getUser(value: Self):"
            "add(collection: &UserCollection, user: &User):"
            "closeLog(file: &File):" "summary(user: User):")
    elseif(filename STREQUAL "lang-memory")
        set(hash 3dac296bc75523bdcf3833a845a3327c0ba8c2fdd3637465f288e8183f6aa6fd)
        set(headers "getAge(user: User):")
    elseif(filename STREQUAL "lang-modules")
        set(hash 46ee3a78e02aeb5f4f0b57303ec99f4e87853649281a75db631b8e236e8e5061)
        set(headers "new(name: str, age: int):" "describe(user: User):"
            "makeGuest():" "getUser(admin: Admin):")
    elseif(filename STREQUAL "lang-stdlib")
        set(hash c2c1e93795c8939cf22ca92aaeb314b60f96f2f533b0afd11a61cd19cb955d0a)
        set(headers "new(name: str, age: int):" "fromJson(json: Json):" "toJson(user: User):")
    elseif(filename STREQUAL "lang-traits")
        set(hash c6b5330f72d8cd82e8807db958b8aca963edb9fe20c0a376f796a6f7af174dc0)
        set(headers "getUser(value: Self):" "getName(value: Self):"
            "getAge(value: Self):" "getAgeRange(value: Self):"
            "getUser(user: User):" "getUser(admin: Admin):" "getName(admin: Admin):")
    elseif(filename STREQUAL "lang-types")
        set(hash d9d2925103455f8d79f3708263faff132a5ffec3d577327aac549ace55ea49ba)
        set(headers "add(collection: &UserCollection, user: &User):" "divide(a: int, b: int):")
    else()
        return()
    endif()
    get_property(verified GLOBAL PROPERTY return_arrows_${filename})
    if(NOT verified)
        file(SHA256 "${ROOT}/docs/v0/${filename}.txt" actual)
        if(NOT actual STREQUAL hash)
            message(FATAL_ERROR "${filename}: frozen return-arrow bridge needs review")
        endif()
        file(READ "${ROOT}/docs/v0/${filename}.txt" original)
        foreach(header IN LISTS headers)
            string(FIND "${original}" "${header}" at)
            if(at LESS 0)
                message(FATAL_ERROR "Missing frozen return-arrow anchor: ${header}")
            endif()
        endforeach()
        set_property(GLOBAL PROPERTY return_arrows_${filename} TRUE)
    endif()
    set(code "${${variable}}")
    foreach(header IN LISTS headers)
        string(LENGTH "${header}" length)
        math(EXPR length "${length} - 1")
        string(SUBSTRING "${header}" 0 ${length} signature)
        string(REPLACE "${header}" "${signature} ->" code "${code}")
    endforeach()
    set(${variable} "${code}" PARENT_SCOPE)
endfunction()
