/* Example plugin.
 * Copyright Red Hat
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
 */

package main

import (
	"C"
	"libguestfs.org/nbdkit"
	"strconv"
	"unsafe"
)

var pluginName = "ramdisk"

type RAMDiskPlugin struct {
	nbdkit.Plugin
}

type RAMDiskConnection struct {
	nbdkit.Connection
}

var size uint64
var size_set = false
var disk []byte

func (p *RAMDiskPlugin) Config(key string, value string) error {
	if key == "size" {
		var err error
		size, err = strconv.ParseUint(value, 0, 64)
		if err != nil {
			return err
		}
		size_set = true
		return nil
	} else {
		return nbdkit.PluginError{Errmsg: "unknown parameter"}
	}
}

func (p *RAMDiskPlugin) ConfigComplete() error {
	if !size_set {
		return nbdkit.PluginError{Errmsg: "size parameter is required"}
	}
	return nil
}

func (p *RAMDiskPlugin) GetReady() error {
	// Allocate the RAM disk.
	disk = make([]byte, size)
	return nil
}

func (p *RAMDiskPlugin) Open(readonly bool) (nbdkit.ConnectionInterface, error) {
	return &RAMDiskConnection{}, nil
}

func (c *RAMDiskConnection) GetSize() (uint64, error) {
	return size, nil
}

// Clients are allowed to make multiple connections safely.
func (c *RAMDiskConnection) CanMultiConn() (bool, error) {
	return true, nil
}

func (c *RAMDiskConnection) PRead(buf []byte, offset uint64,
	flags uint32) error {
	copy(buf, disk[offset:int(offset)+len(buf)])
	return nil
}

// Note that CanWrite is required in golang plugins, otherwise PWrite
// will never be called.
func (c *RAMDiskConnection) CanWrite() (bool, error) {
	return true, nil
}

func (c *RAMDiskConnection) PWrite(buf []byte, offset uint64,
	flags uint32) error {
	copy(disk[offset:int(offset)+len(buf)], buf)
	return nil
}

//----------------------------------------------------------------------
//
// The boilerplate below this line is required by all golang plugins,
// as well as importing "C" and "unsafe" modules at the top of the
// file.

//export plugin_init
func plugin_init() unsafe.Pointer {
	// If your plugin needs to do any initialization, you can
	// either put it here or implement a Load() method.
	// ...

	// Then you must call the following function.
	return nbdkit.PluginInitialize(pluginName, &RAMDiskPlugin{})
}

// This is never(?) called, but must exist.
func main() {}
