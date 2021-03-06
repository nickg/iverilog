/*
 * Copyright (c) 2010 Stephen Williams (steve@icarus.com)
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

# include  "netenum.h"
# include  "compiler.h"
# include  <cassert>

netenum_t::netenum_t(ivl_variable_type_t btype, bool signed_flag,
		     long msb, long lsb, size_t name_count)
: base_type_(btype), signed_flag_(signed_flag), msb_(msb), lsb_(lsb),
  names_(name_count), bits_(name_count)
{
}

netenum_t::~netenum_t()
{
}

bool netenum_t::insert_name(size_t name_idx, perm_string name, const verinum&val)
{
      std::pair<std::map<perm_string,verinum>::iterator, bool> res;

      assert((msb_-lsb_+1) > 0);
      assert(val.has_len() && val.len() == (unsigned)(msb_-lsb_+1));

	// Insert a map of the name to the value. This also gets a
	// flag that returns true if the  name is unique, or false
	// otherwise.
      res = names_map_.insert( make_pair(name,val) );

      assert(name_idx < names_.size() && names_[name_idx] == 0);
      names_[name_idx] = name;

      return res.second;
}

netenum_t::iterator netenum_t::find_name(perm_string name) const
{
      return names_map_.find(name);
}

netenum_t::iterator netenum_t::end_name() const
{
      return names_map_.end();
}

netenum_t::iterator netenum_t::first_name() const
{
      return names_map_.find(names_.front());
}

netenum_t::iterator netenum_t::last_name() const
{
      return names_map_.find(names_.back());
}

perm_string netenum_t::name_at(size_t idx) const
{
      assert(idx < names_.size());
      return names_[idx];
}

perm_string netenum_t::bits_at(size_t idx)
{
      assert(idx < names_.size());

      if (bits_[idx] == 0) {
	    netenum_t::iterator cur = names_map_.find(names_[idx]);

	    vector<char>str (cur->second.len() + 1);
	    for (unsigned bit = 0 ; bit < cur->second.len() ; bit += 1) {
		  switch (cur->second.get(bit)) {
		      case verinum::V0:
			str[bit] = '0';
			break;
		      case verinum::V1:
			str[bit] = '1';
			break;
		      case verinum::Vx:
			str[bit] = 'x';
			break;
		      case verinum::Vz:
			str[bit] = 'z';
			break;
		  }
	    }
	    bits_[idx] = bits_strings.make(&str[0]);
      }

      return bits_[idx];
}
