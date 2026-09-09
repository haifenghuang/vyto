/* Vyto embedded-asset registry.
 *
 * Populated at startup by the generated vyto_assets.c when a build passes
 * --with-assets; empty otherwise, so every lookup misses and callers fall
 * through to disk with one branch. Keys are logical paths relative to the app
 * root, e.g. "assets/logo.png". Amalgamated into vyto_rt (see vyto_rt.c), so
 * the symbols live in the runtime object for the generated asset TU to link. */
#ifndef VYTO_VFS_H
#define VYTO_VFS_H

/* Register one embedded blob. `logical`/`data` are expected to be static (owned
 * by the generated TU / process lifetime); the registry does not copy them. */
void vt_vfs_register(const char *logical, const unsigned char *data, long len);

/* Look up a blob. Matches `key` exactly, or an absolute/longer path whose tail
 * is "/"+key (so "assets/x" and "/app/assets/x" both resolve). Returns 1 and
 * fills *out/*out_len on hit, 0 on miss. out/out_len may be NULL. */
int vt_vfs_get(const char *key, const unsigned char **out, long *out_len);
int vt_vfs_has(const char *key);

/* The same lookup with the suffix arm removed: `key` must equal the registered
 * logical path exactly.
 *
 * The suffix rule above is a convenience for code holding a longer path, but it
 * makes the registry unsafe to query with anything derived from user input: a
 * request for "/whatever/404.html" MATCHES an embedded "404.html" and serves it,
 * because the stored key is a path-component tail of the query. A server that
 * hands a request path to vt_vfs_get therefore answers for keys it never meant
 * to expose. These variants are what such a caller wants — the match is
 * whole-string, so an attacker-supplied prefix cannot reach an embedded blob. */
int vt_vfs_get_exact(const char *key, const unsigned char **out, long *out_len);

/* Volt-friendly accessors: a bare data pointer (NULL on miss) and byte length
   (-1 on miss), so .vt code can query the registry via simple extern decls. */
const unsigned char *vt_vfs_ptr(const char *key);
long vt_vfs_size(const char *key);

/* Copy up to `cap` bytes of the asset into `buf`; returns bytes copied, or -1
   on miss. Lets .vt build a byte[] without exposing the raw pointer. */
long vt_vfs_read(const char *key, unsigned char *buf, long cap);

/* Exact-match forms of the two accessors .vt code reaches (vyto/asset). */
long vt_vfs_size_exact(const char *key);
long vt_vfs_read_exact(const char *key, unsigned char *buf, long cap);

/* Registry iteration (for asset listing). vt_vfs_key returns NULL out of range.
   Keys are the logical paths as registered, e.g. "assets/logo.png". */
int vt_vfs_count(void);
const char *vt_vfs_key(int i);

#endif
