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

(* Set fixed fields in the C plugin struct, for anything which is
 * not a function pointer.
 *)
external set_name : string -> unit = "ocaml_nbdkit_set_name" [@@noalloc]
external set_longname : string -> unit = "ocaml_nbdkit_set_longname" [@@noalloc]
external set_version : string -> unit = "ocaml_nbdkit_set_version" [@@noalloc]
external set_description : string -> unit
  = "ocaml_nbdkit_set_description" [@@noalloc]
external set_config_help : string -> unit
  = "ocaml_nbdkit_set_config_help" [@@noalloc]
external set_magic_config_key : string -> unit
  = "ocaml_nbdkit_set_magic_config_key" [@@noalloc]

(* Set an arbitrary named function pointer field in the C plugin struct.
 *
 * Caution: There is no type checking here, the parameter type
 * declared in [NBDKit.mli] must match what the corresponding
 * [<field_name>_wrapper] function in [plugin.c] calls.
 *)
external set_field : string -> 'a -> unit = "ocaml_nbdkit_set_field" [@@noalloc]

(* Register the plugin. *)
let register_plugin ~name
                    ?longname
                    ?version
                    ?description
                    ?load
                    ?get_ready
                    ?after_fork
                    ?cleanup
                    ?unload
                    ?config
                    ?config_complete
                    ?config_help
                    ?thread_model
                    ?magic_config_key
                    ?preconnect
                    ~open_connection
                    ?close
                    ~get_size
                    ?can_cache
                    ?can_extents
                    ?can_fast_zero
                    ?can_flush
                    ?can_fua
                    ?can_multi_conn
                    ?can_trim
                    ?can_write
                    ?can_zero
                    ?is_rotational
                    ~pread
                    ?pwrite
                    ?flush
                    ?trim
                    ?zero
                    ?extents
                    ?cache
                    ?dump_plugin
                    ?list_exports
                    ?default_export
                    ?export_description
                    () =
  (* Set fields in the C plugin struct. *)
  set_name name;
  set_field "open" open_connection;
  set_field "pread" pread;
  set_field "get_size" get_size;

  let may f = function None -> () | Some a -> f a in
  may set_longname longname;
  may set_version version;
  may set_description description;
  may set_config_help config_help;
  may set_magic_config_key magic_config_key;

  may (set_field "after_fork") after_fork;
  may (set_field "cache") cache;
  may (set_field "can_cache") can_cache;
  may (set_field "can_extents") can_extents;
  may (set_field "can_fast_zero") can_fast_zero;
  may (set_field "can_flush") can_flush;
  may (set_field "can_fua") can_fua;
  may (set_field "can_multi_conn") can_multi_conn;
  may (set_field "can_trim") can_trim;
  may (set_field "can_write") can_write;
  may (set_field "can_zero") can_zero;
  may (set_field "cleanup") cleanup;
  may (set_field "close") close;
  may (set_field "config") config;
  may (set_field "config_complete") config_complete;
  may (set_field "default_export") default_export;
  may (set_field "dump_plugin") dump_plugin;
  may (set_field "export_description") export_description;
  may (set_field "extents") extents;
  may (set_field "flush") flush;
  may (set_field "get_ready") get_ready;
  may (set_field "is_rotational") is_rotational;
  may (set_field "list_exports") list_exports;
  may (set_field "load") load;
  may (set_field "preconnect") preconnect;
  may (set_field "pwrite") pwrite;
  may (set_field "thread_model") thread_model;
  may (set_field "trim") trim;
  may (set_field "unload") unload;
  may (set_field "zero") zero

(* Bindings to nbdkit server functions. *)
external set_error : Unix.error -> unit = "ocaml_nbdkit_set_error" [@@noalloc]
external parse_size : string -> int64 = "ocaml_nbdkit_parse_size"
external parse_bool : string -> bool = "ocaml_nbdkit_parse_bool"
external read_password : string -> string = "ocaml_nbdkit_read_password"
external realpath : string -> string = "ocaml_nbdkit_realpath"
external nanosleep : int -> int -> unit = "ocaml_nbdkit_nanosleep"
external export_name : unit -> string = "ocaml_nbdkit_export_name"
external shutdown : unit -> unit = "ocaml_nbdkit_shutdown" [@@noalloc]
external _debug : string -> unit = "ocaml_nbdkit_debug" [@@noalloc]
let debug fs = ksprintf _debug fs
external version : unit -> string = "ocaml_nbdkit_version"
external peer_pid : unit -> int64 = "ocaml_nbdkit_peer_pid"
external peer_uid : unit -> int64 = "ocaml_nbdkit_peer_uid"
external peer_gid : unit -> int64 = "ocaml_nbdkit_peer_gid"
