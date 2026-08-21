/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: minix_fs.h
 * Description: IR0 kernel source/header file
 */

// ===============================================================================
// IR0 KERNEL - MINIX FILESYSTEM STRUCTURES (Linux 0.0.1 style)
// ===============================================================================

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <ir0/stat.h>
#include <ir0/statfs.h>
#include <ir0/types.h>  // For standard types


#define MINIX_BLOCK_SIZE 1024
#define MINIX_INODE_SIZE 32
#define MINIX_NAME_LEN 14
#define MINIX_DIR_ENTRY_SIZE 16


typedef struct minix_inode
{
    uint16_t i_mode;    // Permisos y tipo de archivo
    uint16_t i_uid;     // ID del usuario propietario
    uint32_t i_size;    // Tamaño del archivo en bytes
    uint32_t i_time;    // Tiempo de modificación
    uint8_t i_gid;      // ID del grupo
    uint8_t i_nlinks;   // Número de enlaces
    uint16_t i_zone[9]; // Zonas/bloques (7 directos + 1 indirecto + 1 doble indirecto)
} minix_inode_t;

typedef struct minix_dir_entry
{
    uint16_t inode;            // Número de inode
    char name[MINIX_NAME_LEN]; // Nombre del archivo
} minix_dir_entry_t;


typedef struct minix_superblock
{
    uint16_t s_ninodes;       // Número de inodes
    uint16_t s_nzones;        // Número de zonas
    uint16_t s_imap_blocks;   // Bloques del bitmap de inodes
    uint16_t s_zmap_blocks;   // Bloques del bitmap de zonas
    uint16_t s_firstdatazone; // Primera zona de datos
    uint16_t s_log_zone_size; // Tamaño de zona en bloques
    uint32_t s_max_size;      // Tamaño máximo de archivo
    uint16_t s_magic;         // Número mágico
} minix_superblock_t;


#define MINIX_IFMT 0170000   /* File type mask (POSIX-compatible) */
#define MINIX_IFDIR 0040000  // Directorio
#define MINIX_IFCHR 0020000  // Dispositivo de caracteres
#define MINIX_IFBLK 0060000  // Dispositivo de bloques
#define MINIX_IFREG 0100000  // Archivo regular
#define MINIX_IFLNK 0120000  // Enlace simbólico
#define MINIX_IFSOCK 0140000 // Socket

// Permisos
#define MINIX_IRWXU 0000700 // Usuario: lectura, escritura, ejecución
#define MINIX_IRUSR 0000400 // Usuario: lectura
#define MINIX_IWUSR 0000200 // Usuario: escritura
#define MINIX_IXUSR 0000100 // Usuario: ejecución

#define MINIX_IRWXG 0000070 // Grupo: lectura, escritura, ejecución
#define MINIX_IRGRP 0000040 // Grupo: lectura
#define MINIX_IWGRP 0000020 // Grupo: escritura
#define MINIX_IXGRP 0000010 // Grupo: ejecución

#define MINIX_IRWXO 0000007 // Otros: lectura, escritura, ejecución
#define MINIX_IROTH 0000004 // Otros: lectura
#define MINIX_IWOTH 0000002 // Otros: escritura
#define MINIX_IXOTH 0000001 // Otros: ejecución


// Verificar si un inode es un directorio
static inline bool minix_is_dir(const minix_inode_t *inode)
{
    return (inode->i_mode & MINIX_IFMT) == MINIX_IFDIR;
}

// Verificar si un inode es un archivo regular
static inline bool minix_is_reg(const minix_inode_t *inode)
{
    return (inode->i_mode & MINIX_IFMT) == MINIX_IFREG;
}

// Verificar si un inode es un dispositivo de caracteres
static inline bool minix_is_chr(const minix_inode_t *inode)
{
    return (inode->i_mode & MINIX_IFMT) == MINIX_IFCHR;
}

// Verificar si un inode es un dispositivo de bloques
static inline bool minix_is_blk(const minix_inode_t *inode)
{
    return (inode->i_mode & MINIX_IFMT) == MINIX_IFBLK;
}

// Obtener permisos de usuario
static inline uint16_t minix_get_uid_perms(const minix_inode_t *inode)
{
    return (inode->i_mode & MINIX_IRWXU) >> 6;
}

// Obtener permisos de grupo
static inline uint16_t minix_get_gid_perms(const minix_inode_t *inode)
{
    return (inode->i_mode & MINIX_IRWXG) >> 3;
}

// Obtener permisos de otros
static inline uint16_t minix_get_oth_perms(const minix_inode_t *inode)
{
    return inode->i_mode & MINIX_IRWXO;
}


// Filesystem availability check
bool minix_fs_is_available(void);
bool minix_fs_is_working(void);

/*
 * Registra el tipo de filesystem MINIX en el VFS (llamado desde vfs.c al
 * inicializar los builtins).
 */
int minix_fs_register(void);

// Filesystem operations
int minix_fs_init(void);
int minix_fs_format(void);
int minix_fs_mkdir(const char *path, mode_t mode);
int minix_fs_ls(const char *path, bool detailed);

int minix_fs_cat(const char *path);
int minix_fs_write_file_len(const char *path, const void *content, size_t content_size);
int minix_fs_write_file(const char *path, const char *content);
int minix_fs_read_file(const char *path, void **data, size_t *size);
int minix_fs_touch(const char *path, mode_t mode);
int minix_fs_rm(const char *path);
int minix_fs_rmdir(const char *path);
int minix_fs_rmdir_force(const char *path);
int minix_fs_stat(const char *pathname, stat_t *buf);
/* Fill Linux-compatible statfs for the mounted MINIX volume. */
int minix_fs_statfs(struct ir0_statfs *buf);
int minix_fs_chown(const char *path, uid_t owner, gid_t group);
int minix_fs_link(const char *oldpath, const char *newpath);
void minix_fs_cleanup(void);

// Operaciones de inodes
/* Returns static storage. Copy immediately; prefer a VFS-serialized caller. */
minix_inode_t *minix_fs_find_inode(const char *pathname);
uint16_t minix_fs_get_inode_number(const char *pathname);
int minix_fs_write_inode(uint16_t inode_num, const minix_inode_t *inode);
int minix_fs_free_inode(uint16_t inode_num);

// Operaciones de directorios
int minix_fs_split_path(const char *pathname, char *parent_path, char *filename);
int minix_fs_add_dir_entry(minix_inode_t *parent_inode, const char *filename, uint16_t inode_num);
int minix_fs_remove_dir_entry(minix_inode_t *parent_inode, const char *filename);
uint16_t minix_fs_find_dir_entry(const minix_inode_t *dir_inode, const char *name);

// Operaciones de bloques
int minix_read_block(uint32_t block_num, void *buffer);
int minix_write_block(uint32_t block_num, const void *buffer);

// Operaciones de zonas
uint32_t minix_alloc_zone(void);
void minix_free_zone(uint32_t zone_num);
bool minix_is_zone_free(uint32_t zone_num);
void minix_mark_zone_used(uint32_t zone_num);

// Operaciones de inodes (bitmap)
uint32_t minix_alloc_inode(void);
void minix_mark_inode_used(uint32_t inode_num);
void minix_mark_inode_free(uint32_t inode_num);
