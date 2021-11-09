(* hey emacs, this is OCaml code: -*- tuareg -*- *)
(* nbdkit OCaml interface
 * Copyright (C) 2014-2020 Red Hat Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of Red Hat nor the names of its contributors may be
 * used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY RED HAT AND CONTRIBUTORS ''AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL RED HAT OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *)

open Printf

type flags = flag list
and flag = May_trim | FUA | Req_one

type fua_flag = FuaNone | FuaEmulate | FuaNative

type cache_flag = CacheNone | CacheEmulate | CacheNop

type thread_model =
| THREAD_MODEL_SERIALIZE_CONNECTIONS
| THREAD_MODEL_SERIALIZE_ALL_REQUESTS
| THREAD_MODEL_SERIALIZE_REQUESTS
| THREAD_MODEL_PARALLEL

type extent = {
  offset : int64;
  length : int64;
  is_hole : bool;
  is_zero : bool;
}

type export = {
  name : string;
  description : string option;
}

type 'a plugin = {
  name : string;
  longname : string;
  version : string;
  description : string;

  load : (unit -> unit) option;
  get_ready : (unit -> unit) option;
  after_fork : (unit -> unit) option;
  cleanup : (unit -> unit) option;
  unload : (unit -> unit) option;

  config : (string -> string -> unit) option;
  config_complete : (unit -> unit) option;
  config_help : string;
  thread_model : (unit -> thread_model) option;

  preconnect : (bool -> unit) option;
  open_connection : (bool -> 'a) option;
  close : ('a -> unit) option;

  get_size : ('a -> int64) option;
  can_cache : ('a -> cache_flag) option;
  can_extents : ('a -> bool) option;
  can_fast_zero : ('a -> bool) option;
  can_flush : ('a -> bool) option;
  can_fua : ('a -> fua_flag) option;
  can_multi_conn : ('a -> bool) option;
  can_trim : ('a -> bool) option;
  can_write : ('a -> bool) option;
  can_zero : ('a -> bool) option;
  is_rotational : ('a -> bool) option;

  pread : ('a -> int32 -> int64 -> flags -> string) option;
  pwrite : ('a -> string -> int64 -> flags -> unit) option;
  flush : ('a -> flags -> unit) option;
  trim : ('a -> int32 -> int64 -> flags -> unit) option;
  zero : ('a -> int32 -> int64 -> flags -> unit) option;
  extents : ('a -> int32 -> int64 -> flags -> extent list) option;
  cache : ('a -> int32 -> int64 -> flags -> unit) option;

  dump_plugin : (unit -> unit) option;
  list_exports : (bool -> bool -> export list) option;
  default_export : (bool -> bool -> string) option;
  export_description : ('a -> string) option;
}

let default_callbacks = {
  name = "";
  longname = "";
  version = "";
  description = "";

  load = None;
  get_ready = None;
  after_fork = None;
  cleanup = None;
  unload = None;

  config = None;
  config_complete = None;
  config_help = "";
  thread_model = None;

  preconnect = None;
  open_connection = None;
  close = None;

  get_size = None;
  can_cache = None;
  can_extents = None;
  can_fast_zero = None;
  can_flush = None;
  can_fua = None;
  can_multi_conn = None;
  can_trim = None;
  can_write = None;
  can_zero = None;
  is_rotational = None;

  pread = None;
  pwrite = None;
  flush = None;
  trim = None;
  zero = None;
  extents = None;
  cache = None;

  dump_plugin = None;
  list_exports = None;
  default_export = None;
  export_description = None;
}

external set_name : string -> unit = "ocaml_nbdkit_set_name" [@@noalloc]
external set_longname : string -> unit = "ocaml_nbdkit_set_longname" [@@noalloc]
external set_version : string -> unit = "ocaml_nbdkit_set_version" [@@noalloc]
external set_description : string -> unit = "ocaml_nbdkit_set_description" [@@noalloc]
external set_config_help : string -> unit = "ocaml_nbdkit_set_config_help" [@@noalloc]

external set_field : string -> 'a -> unit = "ocaml_nbdkit_set_field" [@@noalloc]

let register_plugin plugin =
  (* Check the required fields have been set by the caller. *)
  let required fieldname = function
    | true -> ()
    | false ->
       if plugin.name = "" then
         failwith (sprintf "NBDKit.plugin.%s field is not set" fieldname)
       else
         failwith (sprintf "%s: NBDKit.plugin.%s field is not set"
                     plugin.name fieldname)
  in
  required "name"            (plugin.name <> "");
  required "open_connection" (plugin.open_connection <> None);
  required "get_size"        (plugin.get_size <> None);
  required "pread"           (plugin.pread <> None);

  (* Set fields in the C plugin struct. *)
  set_name plugin.name;
  if plugin.longname <> "" then    set_longname plugin.longname;
  if plugin.version <> "" then     set_version plugin.version;
  if plugin.description <> "" then set_description plugin.description;
  if plugin.config_help <> "" then set_config_help plugin.config_help;

  let may f = function None -> () | Some a -> f a in
  may (set_field "after_fork") plugin.after_fork;
  may (set_field "cache") plugin.cache;
  may (set_field "can_cache") plugin.can_cache;
  may (set_field "can_extents") plugin.can_extents;
  may (set_field "can_fast_zero") plugin.can_fast_zero;
  may (set_field "can_flush") plugin.can_flush;
  may (set_field "can_fua") plugin.can_fua;
  may (set_field "can_multi_conn") plugin.can_multi_conn;
  may (set_field "can_trim") plugin.can_trim;
  may (set_field "can_write") plugin.can_write;
  may (set_field "can_zero") plugin.can_zero;
  may (set_field "cleanup") plugin.cleanup;
  may (set_field "close") plugin.close;
  may (set_field "config") plugin.config;
  may (set_field "config_complete") plugin.config_complete;
  may (set_field "default_export") plugin.default_export;
  may (set_field "dump_plugin") plugin.dump_plugin;
  may (set_field "export_description") plugin.export_description;
  may (set_field "extents") plugin.extents;
  may (set_field "flush") plugin.flush;
  may (set_field "get_ready") plugin.get_ready;
  may (set_field "get_size") plugin.get_size;
  may (set_field "is_rotational") plugin.is_rotational;
  may (set_field "list_exports") plugin.list_exports;
  may (set_field "load") plugin.load;
  may (set_field "open") plugin.open_connection;
  may (set_field "pread") plugin.pread;
  may (set_field "preconnect") plugin.preconnect;
  may (set_field "pwrite") plugin.pwrite;
  may (set_field "thread_model") plugin.thread_model;
  may (set_field "trim") plugin.trim;
  may (set_field "unload") plugin.unload;
  may (set_field "zero") plugin.zero

(* Bindings to nbdkit server functions. *)

external _set_error : int -> unit = "ocaml_nbdkit_set_error" [@@noalloc]

let set_error unix_error =
  (* There's an awkward triple translation going on here, because
   * OCaml Unix.error codes, errno on the host system, and NBD_*
   * errnos are not all the same integer value.  Plus we cannot
   * read the host system errno values from OCaml.
   *)
  let nbd_error =
    match unix_error with
    | Unix.EPERM      -> 1
    | Unix.EIO        -> 2
    | Unix.ENOMEM     -> 3
    | Unix.EINVAL     -> 4
    | Unix.ENOSPC     -> 5
    | Unix.ESHUTDOWN  -> 6
    | Unix.EOVERFLOW  -> 7
    | Unix.EOPNOTSUPP -> 8
    | Unix.EROFS      -> 9
    | Unix.EFBIG      -> 10
    | _               -> 4 (* EINVAL *) in

  _set_error nbd_error

external parse_size : string -> int64 = "ocaml_nbdkit_parse_size"
external parse_bool : string -> bool = "ocaml_nbdkit_parse_bool"
external read_password : string -> string = "ocaml_nbdkit_read_password"
external realpath : string -> string = "ocaml_nbdkit_realpath"
external nanosleep : int -> int -> unit = "ocaml_nbdkit_nanosleep"
external export_name : unit -> string = "ocaml_nbdkit_export_name"
external shutdown : unit -> unit = "ocaml_nbdkit_shutdown" [@@noalloc]

external _debug : string -> unit = "ocaml_nbdkit_debug" [@@noalloc]

let debug fs =
  ksprintf _debug fs

external version : unit -> string = "ocaml_nbdkit_version"
external peer_pid : unit -> int64 = "ocaml_nbdkit_peer_pid"
external peer_uid : unit -> int64 = "ocaml_nbdkit_peer_uid"
external peer_gid : unit -> int64 = "ocaml_nbdkit_peer_gid"
