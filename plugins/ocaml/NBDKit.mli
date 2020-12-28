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

(** Interface between plugins written in OCaml and the nbdkit server.

    Read [nbdkit-ocaml-plugin(3)] first. *)

(** Flags passed from the server to various callbacks. *)
type flags = flag list
and flag = May_trim | FUA | Req_one

type fua_flag = FuaNone | FuaEmulate | FuaNative

type cache_flag = CacheNone | CacheEmulate | CacheNop

(** The type of the extent list returned by {!plugin.extents} *)
type extent = {
  offset : int64;
  length : int64;
  is_hole : bool;
  is_zero : bool;
}

(** The type of the export list returned by {!plugin.list_exports} *)
type export = {
  name : string;
  description : string option;
}

(** The type of the thread model returned by {!plugin.thread_model} *)
type thread_model =
| THREAD_MODEL_SERIALIZE_CONNECTIONS
| THREAD_MODEL_SERIALIZE_ALL_REQUESTS
| THREAD_MODEL_SERIALIZE_REQUESTS
| THREAD_MODEL_PARALLEL

(** The plugin fields and callbacks.

    The ['a] parameter is the handle type returned by your
    [open_connection] method and passed back to all connected calls. *)
type 'a plugin = {
  name : string;                                  (* required *)
  longname : string;
  version : string;
  description : string;

  load : (unit -> unit) option;
  unload : (unit -> unit) option;

  dump_plugin : (unit -> unit) option;

  config : (string -> string -> unit) option;
  config_complete : (unit -> unit) option;
  config_help : string;
  thread_model : (unit -> thread_model) option;

  get_ready : (unit -> unit) option;
  after_fork : (unit -> unit) option;

  preconnect : (bool -> unit) option;
  list_exports : (bool -> bool -> export list) option;
  default_export : (bool -> bool -> string) option;
  open_connection : (bool -> 'a) option;          (* required *)
  close : ('a -> unit) option;

  get_size : ('a -> int64) option;                (* required *)
  export_description : ('a -> string) option;

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

  pread : ('a -> int32 -> int64 -> flags -> string) option;  (* required *)
  pwrite : ('a -> string -> int64 -> flags -> unit) option;
  flush : ('a -> flags -> unit) option;
  trim : ('a -> int32 -> int64 -> flags -> unit) option;
  zero : ('a -> int32 -> int64 -> flags -> unit) option;
  extents : ('a -> int32 -> int64 -> flags -> extent list) option;
  cache : ('a -> int32 -> int64 -> flags -> unit) option;
}

(** The plugin with all fields set to [None], so you can write
    [{ default_callbacks with field1 = Some foo1; field2 = Some foo2 }] *)
val default_callbacks : 'a plugin

(** Register the plugin with nbdkit. *)
val register_plugin : 'a plugin -> unit

(** Set the errno returned over the NBD protocol to the client.

    Notice however that the NBD protocol only supports a small
    handful of errno values.  Any other errno will be translated
    into [EINVAL]. *)
val set_error : Unix.error -> unit

(** Bindings for [nbdkit_parse_size], [nbdkit_parse_bool] and
    [nbdkit_read_password].  See nbdkit-plugin(3) for information
    about these functions.

    On error these functions all raise [Invalid_argument].  The
    actual error is sent to the nbdkit error log and is not
    available from the OCaml code.  It is usually best to let
    the exception escape. *)
(* Note OCaml has functions already for parsing other integers, so
 * there is no need to bind them here.  We only bind the functions
 * which have special abilities in nbdkit: [parse_size] can parse
 * human sizes, [parse_bool] parses a range of nbdkit-specific
 * boolean strings, and [read_password] suppresses echo.
 *)
val parse_size : string -> int64
val parse_bool : string -> bool
val read_password : string -> string

(** Binding for [nbdkit_realpath].
    Returns the canonical path from a path parameter. *)
(* OCaml's [Filename] module can handle [absolute_path]. *)
val realpath : string -> string

(** Binding for [nbdkit_nanosleep].  Sleeps for seconds and nanoseconds. *)
val nanosleep : int -> int -> unit

(** Binding for [nbdkit_export_name].  Returns the name of the
    export as requested by the client. *)
val export_name : unit -> string

(** Binding for [nbdkit_shutdown].  Requests the server shut down. *)
val shutdown : unit -> unit

(** Print a debug message when nbdkit is in verbose mode. *)
val debug : ('a, unit, string, unit) format4 -> 'a

(** Return the version of nbdkit that the plugin was compiled with. *)
val version : unit -> string

(** Binding for [nbdkit_peer_pid]. *)
val peer_pid : unit -> int64

(** Binding for [nbdkit_peer_uid]. *)
val peer_uid : unit -> int64

(** Binding for [nbdkit_peer_gid]. *)
val peer_gid : unit -> int64
