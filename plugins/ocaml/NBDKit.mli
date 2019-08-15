(* hey emacs, this is OCaml code: -*- tuareg -*- *)
(* nbdkit OCaml interface
 * Copyright (C) 2014-2019 Red Hat Inc.
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

(** The interface between nbdkit plugins written in OCaml and nbdkit.

    Reading [nbdkit-ocaml-plugin(3)] is advised. *)

type flags = flag list
and flag = May_trim | FUA | Req_one
(** Flags passed from the server to various callbacks. *)

type fua_flag = FuaNone | FuaEmulate | FuaNative

type cache_flag = CacheNone | CacheEmulate | CacheNop

type extent = {
  offset : int64;
  length : int64;
  is_hole : bool;
  is_zero : bool;
}
(** The type of the extent list returned by [.extents]. *)

type thread_model =
| THREAD_MODEL_SERIALIZE_CONNECTIONS
| THREAD_MODEL_SERIALIZE_ALL_REQUESTS
| THREAD_MODEL_SERIALIZE_REQUESTS
| THREAD_MODEL_PARALLEL
(** The type of the thread model returned by [.thread_model]. *)

type 'a plugin = {
  name : string;                                  (* required *)
  longname : string;
  version : string;
  description : string;

  load : (unit -> unit) option;
  unload : (unit -> unit) option;

  config : (string -> string -> unit) option;
  config_complete : (unit -> unit) option;
  config_help : string;

  open_connection : (bool -> 'a) option;          (* required *)
  close : ('a -> unit) option;

  get_size : ('a -> int64) option;                (* required *)

  can_write : ('a -> bool) option;
  can_flush : ('a -> bool) option;
  is_rotational : ('a -> bool) option;
  can_trim : ('a -> bool) option;

  dump_plugin : (unit -> unit) option;

  can_zero : ('a -> bool) option;
  can_fua : ('a -> fua_flag) option;

  pread : ('a -> int32 -> int64 -> flags -> string) option;  (* required *)
  pwrite : ('a -> string -> int64 -> flags -> unit) option;
  flush : ('a -> flags -> unit) option;
  trim : ('a -> int32 -> int64 -> flags -> unit) option;
  zero : ('a -> int32 -> int64 -> flags -> unit) option;

  can_multi_conn : ('a -> bool) option;

  can_extents : ('a -> bool) option;
  extents : ('a -> int32 -> int64 -> flags -> extent list) option;

  can_cache : ('a -> cache_flag) option;
  cache : ('a -> int32 -> int64 -> flags -> unit) option;

  thread_model : (unit -> thread_model) option;
}
(** The plugin fields and callbacks.  ['a] is the handle type. *)

val default_callbacks : 'a plugin
(** The plugin with all fields set to [None], so you can write
    [{ defaults_callbacks with field1 = Some foo1; field2 = Some foo2 }] *)

val register_plugin : 'a plugin -> unit
(** Register the plugin with nbdkit. *)

val set_error : Unix.error -> unit
(** Set the errno returned over the NBD protocol to the client.

    Notice however that the NBD protocol only supports a small
    handful of errno values.  Any other errno will be translated
    into [EINVAL]. *)

val parse_size : string -> int64
val parse_bool : string -> bool
val read_password : string -> string
(** Bindings for [nbdkit_parse_size], [nbdkit_parse_bool] and
    [nbdkit_read_password].  See nbdkit-plugin(3) for information
    about these functions.

    On error these functions all raise [Invalid_argument].  The
    actual error is sent to the nbdkit error log and is not
    available from the OCaml code.  It is usually best to let
    the exception escape. *)

val debug : ('a, unit, string, unit) format4 -> 'a
(** Print a debug message when nbdkit is in verbose mode. *)
