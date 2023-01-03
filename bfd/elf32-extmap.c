/* extended map support for elf format
   Copyright  2011 Free Software Foundation, Inc.
   Contributed by Horst Lehser (Horst.Lehser@hightec-rt.com).

   This file is part of BFD, the Binary File Descriptor library.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the
   Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
   Boston, MA 02110-1301, USA.  */


#include "sysdep.h"
#include <stdarg.h>
#include "bfd.h"
#include "bfdver.h"
#include "bfdlink.h"
#include "libbfd.h"
#include "elf-bfd.h"
#include "elf32-extmap.h"
#include "elf/tricore.h"

/* If the linker was invoked with -M or -Map, we save the pointer to
   the map file in this variable; used to list allocated bit objects
   and other fancy extensions.  */

FILE *elf32_map_file = (FILE *) NULL;

/* If the linker was invoked with --extmap in addition to -M/-Map, we
   also save the filename of the map file (NULL means stdout).  */

const char *elf32_map_filename = (char *) NULL;

/* True if an extended map file should be produced.  */

bool elf32_extmap_enabled = false;

/* True if the map file should include the version of the linker, the
   date of the link run, and the name of the map file.  */

bool elf32_extmap_header = false;

/* 1 if global symbols should be listed in the map file, 2 if all symbols
   should be listed; symbols are sorted by name.  */

int elf32_extmap_syms_by_name = 0;

/* 1 if global symbols should be listed in the map file, 2 if all symbols
   should be listed; symbols are sorted by address.  */

int elf32_extmap_syms_by_addr = 0;

/* Name of the linker */

const char *elf32_extmap_ld_name = (char *) NULL;

/* Pointer to a function that returns the name of memory region
   to a memory address. */
const char * (*elf32_addr_to_memory_region) (bfd_vma);

/* Symbols to be listed are stored in a dynamically allocated array.  */

static symbol_t *symbol_list;
static int symbol_list_idx = -1;
static int symbol_list_max = 512;

/* This describes memory regions defined by the user; must be kept in
   sync with ld/emultempl/elf32-.em.  */

typedef struct _memreg
{
  /* Name of region.  */
  char *name;

  /* Start of region.  */
  bfd_vma start;

  /* Length of region.  */
  bfd_size_type length;

  /* Number of allocated (used) bytes.  */
  bfd_size_type used;
} memreg_t;


static symbol_t *elf32_new_symentry (void);


static int elf32_extmap_sort_addr (const void *, const void *);

static int elf32_extmap_sort_name (const void *, const void *);

static bool elf32_extmap_add_sym (struct bfd_link_hash_entry *, void *);


static unsigned int
elf32_get_flag_core_number(int other ATTRIBUTE_UNUSED)
{
#ifdef STT_EXPORT_FUNC
  return ELF_STO_READ_CORE_NUMBER(other);
#else
  return 0;
#endif
}


/* Allocate and return a new symbol entry (only used when an extended map
   file containing symbol listings was requested).  */


static symbol_t *
elf32_new_symentry (void)
{
  if (symbol_list_idx == -1)
    {
      symbol_list =
        (symbol_t *) bfd_malloc (symbol_list_max * sizeof (symbol_t));
      BFD_ASSERT (symbol_list);
      symbol_list_idx = 0;
    }
  else if (++symbol_list_idx == symbol_list_max)
    {
      symbol_list_max <<= 2;
      symbol_list =
        (symbol_t *) bfd_realloc (symbol_list,
      				  symbol_list_max * sizeof (symbol_t));
      BFD_ASSERT (symbol_list);
    }

  return &symbol_list[symbol_list_idx];
}

/* This is called by qsort when sorting symbol_list by address.  */

static int
elf32_extmap_sort_addr (const void *p1, const void *p2)
{
  symbol_t *s1 = (symbol_t *) p1, *s2 = (symbol_t *) p2;
  int cmp;

  if (s1->address == s2->address)
    {
      cmp =  strcmp (s1->name, s2->name);
      if (cmp == 0)
        if (s1->is_static != s2->is_static)
          {
            /* we insert local definitions before global */
            return (s1->is_static)? -1:1;
          }
      return cmp;
    }
  else if (s1->address < s2->address)
    return -1;

  return 1;
}

/* This is called by qsort when sorting symbol_list by name.  */

static int
elf32_extmap_sort_name (const void *p1, const void *p2)
{
  symbol_t *s1 = (symbol_t *) p1, *s2 = (symbol_t *) p2;
  int cv;

  cv = strcmp (s1->name, s2->name);
  if (cv == 0)
    {
      if (!s1->is_static && s2->is_static)
        return -1;
      else if (s1->is_static && !s2->is_static)
        return 1;

      return (int) (s1->address - s2->address);
    }

  return cv;
}

static Elf_Internal_Sym *
retrieve_local_syms (bfd *input_bfd)
{
  Elf_Internal_Shdr *symtab_hdr;
  Elf_Internal_Sym *isymbuf;
  size_t locsymcount;

  symtab_hdr = &elf_tdata (input_bfd)->symtab_hdr;
  locsymcount = symtab_hdr->sh_info;

  isymbuf = (Elf_Internal_Sym *) symtab_hdr->contents;
  if (isymbuf == NULL && locsymcount != 0)
    isymbuf = bfd_elf_get_elf_syms (input_bfd, symtab_hdr, locsymcount, 0,
				    NULL, NULL, NULL);

  /* Save the symbols for this input file so they won't be read again.  */
  if (isymbuf && isymbuf != (Elf_Internal_Sym *) symtab_hdr->contents)
    symtab_hdr->contents = (unsigned char *) isymbuf;

  return isymbuf;
}
/* This is the callback function for bfd_link_hash_traverse; it adds
   the global symbol pointed to by ENTRY to the symbol list.  */

static bool
elf32_extmap_add_sym (struct bfd_link_hash_entry *entry, void * link_info ATTRIBUTE_UNUSED)
{
  struct elf_link_hash_entry *h = (struct elf_link_hash_entry *) entry;
  symbol_t *sym;

  if (((h->root.type != bfd_link_hash_defined)
       && (h->root.type != bfd_link_hash_defweak))
      || (bfd_section_flags(h->root.u.def.section)
          & SEC_EXCLUDE)
      || !strcmp (h->root.u.def.section->name, ".boffs")
      || (h->root.u.def.section->output_section == NULL))
    return true;
  sym = elf32_new_symentry ();
  sym->name = h->root.root.string;
  sym->address = (h->root.u.def.section->output_section->vma
                  + h->root.u.def.section->output_offset
                  + h->root.u.def.value);
  sym->region_name = "*default*";
  sym->is_static = false;
  sym->size = h->size;
  sym->section = h->root.u.def.section;

  sym->is_exported = ((ELF_ST_VISIBILITY (h->other) != STV_HIDDEN) && elf32_get_flag_core_number (h->other));
  sym->other = (sym->other & ~ELF_ST_VISIBILITY (-1)) | ELF_ST_VISIBILITY (h->other);
  sym->is_unique = h->unique_global;
  sym->type = h->type;

  return true;
}

/* The initial part of tricore_user_section_struct has to be identical with
   fat_user_section_struct from ld.h  */
typedef struct tricore_user_section_struct {
  void *head;
  void *tail;
  unsigned long count;
  const char *region_name;
  const flagword region_flags;
} tricore_section_userdata_type;


void
elf32_collect_extmap_info (struct bfd_link_info* info,
                           p_extmap_info extmap_inf,
                           const char *(*addr_to_mem)(bfd_vma))
{
  symbol_t *sym;
  bfd_vma max_size = 0;
  char size_str[20];
  int i;
  size_t len;

  if (!elf32_extmap_enabled)
    return;

  extmap_inf->maxlen_size = 4;
  extmap_inf->maxlen_symname = 4;
  extmap_inf->maxlen_memreg = 6;
  extmap_inf->maxlen_osecname = 5;
  extmap_inf->maxlen_isecname = 5;
  extmap_inf->maxlen_modname = 12;
  extmap_inf->bit_seen = false;

  elf32_addr_to_memory_region = addr_to_mem;

  bfd_link_hash_traverse (info->hash, elf32_extmap_add_sym,
                          (void *) info);

  Elf_Internal_Shdr *symtab_hdr, *strtab_hdr;
  Elf_Internal_Sym *isymbuf, *isym, *isymend;
  bfd *ibfd;
  asection *boffs, *section;
  unsigned int boffs_shndx = -1;
  unsigned char *strtab;

  for (ibfd = info->input_bfds; ibfd; ibfd = ibfd->link.next)
    {
      if ((boffs = bfd_get_section_by_name (ibfd, ".boffs")) != NULL)
        boffs_shndx = _bfd_elf_section_from_bfd_section (ibfd, boffs);

      symtab_hdr = &elf_tdata (ibfd)->symtab_hdr;
      if (symtab_hdr->sh_info == 0)
        continue;  /* Input object has no local symbols.  */

      isymbuf = retrieve_local_syms(ibfd);
      if (isymbuf == NULL)
        continue;  /* This error will be catched elsewhere.  */

      isymend = isymbuf + symtab_hdr->sh_info;
      strtab_hdr = &elf_tdata (ibfd)->strtab_hdr;
      strtab = strtab_hdr->contents;
      if (strtab == NULL)
        {
          if (strtab_hdr->sh_size > 1)
            {
              (void)bfd_elf_string_from_elf_section(ibfd, symtab_hdr->sh_link,1);
              // bfd_elf_get_str_section (ibfd, symtab_hdr->sh_link);
              strtab = strtab_hdr->contents;
            }
          else
            continue;
        }
      BFD_ASSERT (strtab);

      for (isym = isymbuf; isym < isymend; ++isym)
        {
          if (isym->st_shndx == SHN_ABS)
            section = bfd_abs_section_ptr;
          else
            section = bfd_section_from_elf_index (ibfd, isym->st_shndx);
          if ((section == NULL)
              || (section->output_section == NULL)
              || (isym->st_name == 0)
              || (ELF_ST_TYPE (isym->st_info) == STT_FILE)
              || (boffs && (isym->st_shndx == boffs_shndx))
              || (bfd_section_flags (section) & SEC_EXCLUDE))
            continue;

          sym = elf32_new_symentry ();
          sym->name = (char *)(strtab + isym->st_name);
          sym->address = (section->output_section->vma
                          + section->output_offset
                          + isym->st_value);
          sym->region_name = "*default*";
          sym->is_static = true;
          sym->size = isym->st_size;
          sym->section = section;
          sym->is_exported = false;
          sym->is_unique = false;
          sym->type = 0;
        }
    }

  /* Compute memory regions, alignment and maximum name lengths.  */
  for (sym = symbol_list, i = 0; i <= symbol_list_idx; ++sym, ++i)
    {
      if ((len = strlen (sym->name)) > extmap_inf->maxlen_symname)
        extmap_inf->maxlen_symname = len;

      if (!strcmp (sym->section->name, "*ABS*"))
        sym->region_name = "*ABS*";
      else if (elf32_addr_to_memory_region)
        {
          sym->region_name = elf32_addr_to_memory_region(sym->address);
        }
      else
        {
	  tricore_section_userdata_type *ud;
          ud = bfd_section_userdata(sym->section->output_section);
          if (ud)
            sym->region_name = ud->region_name;
          else
            sym->region_name = "NOREGION";
        }

      if ((len = strlen (sym->region_name)) > extmap_inf->maxlen_memreg)
        extmap_inf->maxlen_memreg = len;

      if (sym->size > max_size)
        max_size = sym->size;

      if ((len = strlen (sym->section->name)) > extmap_inf->maxlen_isecname)
        extmap_inf->maxlen_isecname = len;

      if ((len = strlen (sym->section->output_section->name))
          > extmap_inf->maxlen_osecname)
        extmap_inf->maxlen_osecname = len;

      if ((sym->section->owner != NULL)
          && ((len = strlen (bfd_get_filename (sym->section->owner)))
              > extmap_inf->maxlen_modname))
        extmap_inf->maxlen_modname = len;

      if ((sym->address & 1)
          || (sym->size & 1)
          || !strcmp (sym->section->name, "*ABS*"))
        sym->align = 1;
      else if ((bfd_section_flags (sym->section)
                & SEC_CODE))
        sym->align = 2;
      else if (!(sym->address & 7))
        sym->align = 8;
      else if (!(sym->address & 3))
        sym->align = 4;
      else
        sym->align = 2;

      if ((sym->size != 0) && (sym->size <= 8))
        while (sym->align > (int) sym->size)
          sym->align >>= 1;
    }

  if ((extmap_inf->maxlen_size = sprintf (size_str, "%ld", max_size)) < 4)
    extmap_inf->maxlen_size = 4;

  extmap_inf->symbol_list = symbol_list;  
}

void
elf32_print_extmap_header (FILE *out,
                           const char *mapfile)
{
  if (! elf32_extmap_enabled)
    return;

  fprintf (out, "\n----- BEGIN EXTENDED MAP LISTING -----\n\n");

  if (elf32_extmap_header)
    {
      time_t now;
      char *tbuf;
      elf32_map_filename = mapfile;

      time (&now);
      tbuf = ctime (&now);
      fprintf (out, ">>> Linker and link run information\n\n");
      fprintf (out, "Linker version: ");
      fprintf (out, _("GNU ld version %s using BFD version %s\n"),
           BFD_VERSION_STRING, BFD_VERSION_STRING);
      fprintf (out, "Name of linker executable: %s\n",
              elf32_extmap_ld_name);
      fprintf (out, "Date of link run: %s", tbuf);
      fprintf (out, "Name of linker map file: %s\n",
              elf32_map_filename);
      fprintf (out, "\n");
    }
}

void
elf32_print_extmap_info (FILE* out,
                         p_extmap_info extmap_info,
                         symbol_list_order order_by)
{
  int granularity = (order_by == ORDER_BY_ADDRESS) ?
                     elf32_extmap_syms_by_addr :
                     elf32_extmap_syms_by_name;

  if (granularity)
    {
      int i, max_chars;
      symbol_t *sym = extmap_info->symbol_list;

      qsort (sym, symbol_list_idx + 1, sizeof (symbol_t),
             (order_by == ORDER_BY_ADDRESS) ? elf32_extmap_sort_addr : elf32_extmap_sort_name);

      fprintf (out, ">>> Symbols (global %s; sorted by %s)\n\n",
               (granularity == 1)
                ? "only" : "(S = g) and static (S = l)",
               (order_by == ORDER_BY_ADDRESS) ? "address" : "name");

      max_chars = (10 + (extmap_info->bit_seen ? 2 : 0) + 1 + 10 + 1
                   + extmap_info->maxlen_size + 1
                   + ((granularity == 1) ? 0 : 2)
                   + extmap_info->maxlen_symname + 1
                   + extmap_info->maxlen_memreg + 1
                   + extmap_info->maxlen_osecname + 1
                   + extmap_info->maxlen_isecname + 1
                   + extmap_info->maxlen_modname);

      for (i = 0; i < max_chars; ++i)
        fputc ('=', out);

      fprintf (out, "\n");
      fprintf (out, "%-*s%s %-*s %*s %s%-*s %-*s %-*s %-*s %-*s\n",
      	       10, "Start",
               extmap_info->bit_seen ? "  " : "",
	       10, "End",
               (int)extmap_info->maxlen_size, "Size",
               (granularity == 1) ? "" : "S ",
               (int)extmap_info->maxlen_symname, "Name",
               (int)extmap_info->maxlen_memreg, "Memory",
               (int)extmap_info->maxlen_osecname, "O-Sec",
               (int)extmap_info->maxlen_isecname, "I-Sec",
               (int)extmap_info->maxlen_modname, "Input object");

      for (i = 0; i < max_chars; ++i)
        fputc ('=', out);

      fprintf (out, "\n");

      for (i = 0; i <= symbol_list_idx; ++sym, ++i)
        {
          if ((granularity == 1) && sym->is_static)
	    continue;

	  fprintf (out, "0x%08lx ", sym->address);
	  fprintf (out, "0x%08lx %*ld %s%-*s %-*s %-*s %-*s %s\n",
	  	   sym->address + sym->size - ((sym->size > 0) ? 1 : 0),
                   (int)extmap_info->maxlen_size, sym->size,
                   (granularity == 1)
		    ? ""
		    : (sym->is_static ? "l " : "g "),
                   (int)extmap_info->maxlen_symname, sym->name,
                   (int)extmap_info->maxlen_memreg, sym->region_name,
                   (int)extmap_info->maxlen_osecname, sym->section->output_section->name,
                   (int)extmap_info->maxlen_isecname, sym->section->name,
		   (sym->section->owner == NULL)
		    ? "*ABS*"
		    : bfd_get_filename (sym->section->owner));
	}

      fprintf (out, "\n");
    }
}

void
elf32_print_extmap_footer (FILE *out)
{
  if (elf32_extmap_enabled)
    fprintf (out, "\n----- END EXTENDED MAP LISTING -----\n\n");
}


/* This parses the sub-options to the --extmap option.  */

char *
elf32_parse_extmap_args (char *args, const char *prog_name)
{
  char *cp;

  if ((args == NULL) || (*args == '\0'))
    return NULL;

  for (cp = args; *cp; ++cp)
    if (*cp == 'a')
      {
        elf32_extmap_header = true;
	elf32_extmap_syms_by_name = 2;
	elf32_extmap_syms_by_addr = 2;
      }
    else if (*cp == 'h')
      elf32_extmap_header = true;
    else if (*cp == 'L')
      elf32_extmap_syms_by_name = 1;
    else if (*cp == 'l')
      elf32_extmap_syms_by_name = 2;
    else if (*cp == 'N')
      elf32_extmap_syms_by_addr = 1;
    else if (*cp == 'n')
      elf32_extmap_syms_by_addr = 2;
    else
      {
        cp[1] = '\0';
        return cp;
      }

  elf32_extmap_enabled = true;
  elf32_extmap_ld_name = prog_name;
  return NULL;
}
