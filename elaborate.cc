/*
 * Copyright (c) 1998-2008 Stephen Williams (steve@icarus.com)
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

# include "config.h"

/*
 * Elaboration takes as input a complete parse tree and the name of a
 * root module, and generates as output the elaborated design. This
 * elaborated design is presented as a Module, which does not
 * reference any other modules. It is entirely self contained.
 */

# include  <typeinfo>
# include  <cstdlib>
# include  <sstream>
# include  <list>
# include  "pform.h"
# include  "PEvent.h"
# include  "PGenerate.h"
# include  "PSpec.h"
# include  "netlist.h"
# include  "netmisc.h"
# include  "util.h"
# include  "parse_api.h"
# include  "compiler.h"
# include  "ivl_assert.h"


static Link::strength_t drive_type(PGate::strength_t drv)
{
      switch (drv) {
	  case PGate::HIGHZ:
	    return Link::HIGHZ;
	  case PGate::WEAK:
	    return Link::WEAK;
	  case PGate::PULL:
	    return Link::PULL;
	  case PGate::STRONG:
	    return Link::STRONG;
	  case PGate::SUPPLY:
	    return Link::SUPPLY;
	  default:
	    assert(0);
      }
      return Link::STRONG;
}


void PGate::elaborate(Design*des, NetScope*scope) const
{
      cerr << "internal error: what kind of gate? " <<
	    typeid(*this).name() << endl;
}

/*
 * Elaborate the continuous assign. (This is *not* the procedural
 * assign.) Elaborate the lvalue and rvalue, and do the assignment.
 */
void PGAssign::elaborate(Design*des, NetScope*scope) const
{
      assert(scope);

      NetExpr* rise_time, *fall_time, *decay_time;
      eval_delays(des, scope, rise_time, fall_time, decay_time, true);

      Link::strength_t drive0 = drive_type(strength0());
      Link::strength_t drive1 = drive_type(strength1());

      assert(pin(0));
      assert(pin(1));

	/* Elaborate the l-value. */
      NetNet*lval = pin(0)->elaborate_lnet(des, scope);
      if (lval == 0) {
	    return;
      }

      assert(lval->pin_count() == 1);

      if (debug_elaborate) {
	    cerr << get_fileline() << ": debug: PGAssign: elaborated l-value"
		 << " width=" << lval->vector_width()
		 << ", type=" << lval->data_type() << endl;
      }

	/* Handle the special case that the rval is simply an
	   identifier. Get the rval as a NetNet, then use NetBUFZ
	   objects to connect it to the l-value. This is necessary to
	   direct drivers. This is how I attach strengths to the
	   assignment operation. */
      if (const PEIdent*id = dynamic_cast<const PEIdent*>(pin(1))) {
	    NetNet*rid = id->elaborate_net(des, scope, lval->vector_width(),
					   0, 0, 0, Link::STRONG,
					   Link::STRONG);
	    if (rid == 0) {
		  des->errors += 1;
		  return;
	    }

	      /* Cast the right side when needed. */
	    if ((lval->data_type() == IVL_VT_REAL &&
	         rid->data_type() != IVL_VT_REAL)) {
		  rid = cast_to_real(des, scope, rid);
	    } else if ((lval->data_type() != IVL_VT_REAL &&
	                rid->data_type() == IVL_VT_REAL)) {
		  rid = cast_to_int(des, scope, rid, lval->vector_width());
	    }

	    ivl_assert(*this, rid);
	    if (rid->pin_count() != 1) {
		  cerr << get_fileline() << ": internal error: "
		       << "Invalid elaborate_net results here:" << endl;
		  rid->dump_net(cerr, 4);
		  des->errors += 1;
		  return;
	    }
	    ivl_assert(*this, rid->pin_count() == 1);

	      /* If the right hand net is the same type as the left
		 side net (i.e., WIRE/WIRE) then it is enough to just
		 connect them together. Otherwise, put a bufz between
		 them to carry strengths from the rval.

		 While we are at it, handle the case where the r-value
		 is not as wide as the l-value by padding with a
		 constant-0. */

	    unsigned cnt = lval->vector_width();
	    if (rid->vector_width() < cnt)
		  cnt = rid->vector_width();

	    bool need_driver_flag = false;

	      /* If the device is linked to itself, a driver is
		 needed. Should I print a warning here? */
	    if (lval->pin(0) .is_linked (rid->pin(0)))
		  need_driver_flag = true;

	      /* If the nets are different type (i.e., reg vs. tri) then
		 a driver is needed. */
	    if (rid->type() != lval->type())
		  need_driver_flag = true;

	      /* If there is a delay, then I need a driver to carry
		 it. */
	    if (rise_time || fall_time || decay_time)
		  need_driver_flag = true;

	      /* If there is a strength to be carried, then I need a
		 driver to carry that strength. */
	    if (rid->pin(0).drive0() != drive0)
		  need_driver_flag = true;

	    if (rid->pin(0).drive1() != drive1)
		  need_driver_flag = true;

	      /* If the r-value is more narrow then the l-value, pad
		 it to the desired width. */
	    if (cnt < lval->vector_width()) {
		  if (lval->get_signed() && rid->get_signed()) {

			unsigned use_width = lval->vector_width();

			if (debug_elaborate)
			      cerr << get_fileline() << ": debug: PGassign "
				   << "Generate sign-extend node." << endl;

			rid = pad_to_width_signed(des, rid, use_width);

		  } else {

			if (debug_elaborate)
			      cerr << get_fileline() << ": debug: PGAssign "
				   << "Unsigned pad r-value from "
				   << cnt << " bits to "
				   << lval->vector_width() << " bits." << endl;

			NetNet*tmp = pad_to_width(des, rid,
						  lval->vector_width());
			rid = tmp;
		  }

	    } else if (cnt < rid->vector_width()) {

		  if (debug_elaborate)
			cerr << get_fileline() << ": debug: PGAssign "
			     << "Truncate r-value from "
			     << cnt << " bits to "
			     << lval->vector_width() << " bits." << endl;

		  NetNet*tmp = crop_to_width(des, rid, lval->vector_width());
		  rid = tmp;
	    }

	    if (! need_driver_flag) {

		    /* Don't need a driver, presumably because the
		       r-value already has the needed drivers. Just
		       hook things up. If the r-value is too narrow
		       for the l-value, then sign extend it or zero
		       extend it, whichever makes sense. */

		  if (debug_elaborate) {
			cerr << get_fileline() << ": debug: PGAssign: "
			     << "Connect lval directly to "
			     << id->path() << endl;
		  }

		  connect(lval->pin(0), rid->pin(0));

	    } else {
		    /* Do need a driver. Use BUFZ objects to carry the
		       strength and delays. */

		  if (debug_elaborate) {
			cerr << get_fileline() << ": debug: PGAssign: "
			     << "Connect lval to " << id->path()
			     << " through bufz. delay=(";
			if (rise_time)
			      cerr << *rise_time << ":";
			else
			      cerr << "<none>:";
			if (fall_time)
			      cerr << *fall_time << ":";
			else
			      cerr << "<none>:";
			if (decay_time)
			      cerr << *decay_time;
			else
			      cerr << "<none>";
			cerr << ")" << endl;
		  }

		  NetBUFZ*dev = new NetBUFZ(scope, scope->local_symbol(),
					    rid->vector_width());
		  connect(lval->pin(0), dev->pin(0));
		  connect(rid->pin(0),  dev->pin(1));
		  dev->rise_time(rise_time);
		  dev->fall_time(fall_time);
		  dev->decay_time(decay_time);
		  dev->pin(0).drive0(drive0);
		  dev->pin(0).drive1(drive1);
		  des->add_node(dev);

	    }

	    return;
      }

	/* Elaborate the r-value. Account for the initial decays,
	   which are going to be attached to the last gate before the
	   generated NetNet. */
      NetNet*rval = pin(1)->elaborate_net(des, scope,
					  lval->vector_width(),
					  0, 0, 0,
					  drive0, drive1);
      if (rval == 0) {
	    cerr << get_fileline() << ": error: Unable to elaborate r-value: "
		 << *pin(1) << endl;
	    des->errors += 1;
	    return;
      }

      if (debug_elaborate) {
	    cerr << get_fileline() << ": debug: PGAssign: elaborated r-value"
		 << " width="<<rval->vector_width()
		 << ", type="<< rval->data_type() << endl;
      }

      assert(lval && rval);
      assert(rval->pin_count() == 1);

	/* Cast the right side when needed. */
      if ((lval->data_type() == IVL_VT_REAL &&
           rval->data_type() != IVL_VT_REAL)) {
	    rval = cast_to_real(des, scope, rval);
      } else if ((lval->data_type() != IVL_VT_REAL &&
                  rval->data_type() == IVL_VT_REAL)) {
	    rval = cast_to_int(des, scope, rval, lval->vector_width());
      }

	/* If the r-value insists on being smaller then the l-value
	   (perhaps it is explicitly sized) the pad it out to be the
	   right width so that something is connected to all the bits
	   of the l-value. */
      if (lval->vector_width() > rval->vector_width())
	    rval = pad_to_width(des, rval, lval->vector_width());

	/* If, on the other hand, the r-value insists on being
	   LARGER then the l-value, use a part select to chop it down
	   down to size. */
      if (lval->vector_width() < rval->vector_width()) {
	    NetPartSelect*tmp = new NetPartSelect(rval, 0,lval->vector_width(),
						  NetPartSelect::VP);
	    des->add_node(tmp);
	    tmp->set_line(*this);
	    NetNet*osig = new NetNet(scope, scope->local_symbol(),
				     NetNet::TRI, lval->vector_width());
	    osig->set_line(*this);
	    osig->data_type(rval->data_type());
	    connect(osig->pin(0), tmp->pin(0));
	    rval = osig;
      }

	/* If there is a rise/fall/decay time, then attach that delay
	   to the drivers for this net. */
      if (rise_time || fall_time || decay_time) {
	    rval->pin(0).drivers_delays(rise_time, fall_time, decay_time);
      }

      connect(lval->pin(0), rval->pin(0));

      if (lval->local_flag())
	    delete lval;

}

/*
 * Elaborate a Builtin gate. These normally get translated into
 * NetLogic nodes that reflect the particular logic function.
 */
void PGBuiltin::elaborate(Design*des, NetScope*scope) const
{
      unsigned count = 1;
      unsigned instance_width = 1;
      long low = 0, high = 0;
      string name = string(get_name());

      if (name == "")
	    name = scope->local_symbol();

	/* If the Verilog source has a range specification for the
	   gates, then I am expected to make more than one
	   gate. Figure out how many are desired. */
      if (msb_) {
	    NetExpr*msb_exp = elab_and_eval(des, scope, msb_, -1);
	    NetExpr*lsb_exp = elab_and_eval(des, scope, lsb_, -1);

	    NetEConst*msb_con = dynamic_cast<NetEConst*>(msb_exp);
	    NetEConst*lsb_con = dynamic_cast<NetEConst*>(lsb_exp);

	    if (msb_con == 0) {
		  cerr << get_fileline() << ": error: Unable to evaluate "
			"expression " << *msb_ << endl;
		  des->errors += 1;
		  return;
	    }

	    if (lsb_con == 0) {
		  cerr << get_fileline() << ": error: Unable to evaluate "
			"expression " << *lsb_ << endl;
		  des->errors += 1;
		  return;
	    }

	    verinum msb = msb_con->value();
	    verinum lsb = lsb_con->value();

	    delete msb_exp;
	    delete lsb_exp;

	    if (msb.as_long() > lsb.as_long())
		  count = msb.as_long() - lsb.as_long() + 1;
	    else
		  count = lsb.as_long() - msb.as_long() + 1;

	    low = lsb.as_long();
	    high = msb.as_long();

	    if (debug_elaborate) {
		  cerr << get_fileline() << ": debug: PGBuiltin: Make array "
		       << "[" << high << ":" << low << "]"
		       << " of " << count << " gates for " << name << endl;
	    }
      }

	/* Now we have a gate count. Elaborate the output expression
	   only. We do it early so that we can see if we can make a
	   wide gate instead of an array of gates. */

      if (pin(0) == 0) {
	    cerr << get_fileline() << ": error: Logic gate port "
	            "expressions are not optional." << endl;
	    des->errors += 1;
	    return;
      }
      NetNet*lval_sig = pin(0)->elaborate_lnet(des, scope);
      assert(lval_sig);

	/* Detect the special case that the l-value width exactly
	   matches the gate count. In this case, we will make a single
	   gate that has the desired vector width. */
      if (lval_sig->vector_width() == count) {
	    instance_width = count;
	    count = 1;

	    if (debug_elaborate && instance_width != 1)
		  cerr << get_fileline() << ": debug: PGBuiltin: "
			"Collapsed gate array into single wide "
			"(" << instance_width << ") instance." << endl;
      }

	/* Allocate all the netlist nodes for the gates. */
      NetNode**cur = new NetNode*[count];
      assert(cur);

	/* Calculate the gate delays from the delay expressions
	   given in the source. For logic gates, the decay time
	   is meaningless because it can never go to high
	   impedance. However, the bufif devices can generate
	   'bz output, so we will pretend that anything can.

	   If only one delay value expression is given (i.e., #5
	   nand(foo,...)) then rise, fall and decay times are
	   all the same value. If two values are given, rise and
	   fall times are use, and the decay time is the minimum
	   of the rise and fall times. Finally, if all three
	   values are given, they are taken as specified. */

      NetExpr* rise_time, *fall_time, *decay_time;
      eval_delays(des, scope, rise_time, fall_time, decay_time);

      struct attrib_list_t*attrib_list = 0;
      unsigned attrib_list_n = 0;
      attrib_list = evaluate_attributes(attributes, attrib_list_n,
					des, scope);

	/* Now make as many gates as the bit count dictates. Give each
	   a unique name, and set the delay times. */

      for (unsigned idx = 0 ;  idx < count ;  idx += 1) {
	    ostringstream tmp;
	    unsigned index;
	    if (low < high)
		  index = low + idx;
	    else
		  index = low - idx;

	    tmp << name << "<" << index << ">";
	    perm_string inm = lex_strings.make(tmp.str());

	    switch (type()) {
		case AND:
		  if (pin_count() < 2) {
		      cerr << get_fileline() << ": error: the AND "
		              "primitive must have an input." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::AND, instance_width);
		  break;
		case BUF:
		  if (pin_count() > 2) {
		      cerr << get_fileline() << ": sorry: multiple output BUF "
		              "primitives are not supported." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::BUF, instance_width);
		  break;
		case BUFIF0:
		  if (pin_count() != 3) {
		      cerr << get_fileline() << ": error: the BUFIF0 "
		              "primitive must have three arguments." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::BUFIF0, instance_width);
		  break;
		case BUFIF1:
		  if (pin_count() != 3) {
		      cerr << get_fileline() << ": error: the BUFIF1 "
		              "primitive must have three arguments." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::BUFIF1, instance_width);
		  break;
		case CMOS:
		  if (pin_count() != 4) {
		      cerr << get_fileline() << ": error: the CMOS "
		              "primitive must have four arguments." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::CMOS, instance_width);
		  break;
		case NAND:
		  if (pin_count() < 2) {
		      cerr << get_fileline() << ": error: the NAND "
		              "primitive must have an input." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::NAND, instance_width);
		  break;
		case NMOS:
		  if (pin_count() != 3) {
		      cerr << get_fileline() << ": error: the NMOS "
		              "primitive must have three arguments." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::NMOS, instance_width);
		  break;
		case NOR:
		  if (pin_count() < 2) {
		      cerr << get_fileline() << ": error: the NOR "
		              "primitive must have an input." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::NOR, instance_width);
		  break;
		case NOT:
		  if (pin_count() > 2) {
		      cerr << get_fileline() << ": sorry: multiple output NOT "
		              "primitives are not supported." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::NOT, instance_width);
		  break;
		case NOTIF0:
		  if (pin_count() != 3) {
		      cerr << get_fileline() << ": error: the NOTIF0 "
		              "primitive must have three arguments." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::NOTIF0, instance_width);
		  break;
		case NOTIF1:
		  if (pin_count() != 3) {
		      cerr << get_fileline() << ": error: the NOTIF1 "
		              "primitive must have three arguments." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::NOTIF1, instance_width);
		  break;
		case OR:
		  if (pin_count() < 2) {
		      cerr << get_fileline() << ": error: the OR "
		              "primitive must have an input." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::OR, instance_width);
		  break;
		case RCMOS:
		  if (pin_count() != 4) {
		      cerr << get_fileline() << ": error: the RCMOS "
		              "primitive must have four arguments." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::RCMOS, instance_width);
		  break;
		case RNMOS:
		  if (pin_count() != 3) {
		      cerr << get_fileline() << ": error: the RNMOS "
		              "primitive must have three arguments." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::RNMOS, instance_width);
		  break;
		case RPMOS:
		  if (pin_count() != 3) {
		      cerr << get_fileline() << ": error: the RPMOS "
		              "primitive must have three arguments." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::RPMOS, instance_width);
		  break;
		case PMOS:
		  if (pin_count() != 3) {
		      cerr << get_fileline() << ": error: the PMOS "
		              "primitive must have three arguments." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::PMOS, instance_width);
		  break;
		case PULLDOWN:
		  if (pin_count() > 1) {
		      cerr << get_fileline() << ": sorry: multiple output PULLDOWN "
		              "primitives are not supported." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::PULLDOWN,
					      instance_width);
		  break;
		case PULLUP:
		  if (pin_count() > 1) {
		      cerr << get_fileline() << ": sorry: multiple output PULLUP "
		              "primitives are not supported." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::PULLUP, instance_width);
		  break;
		case XNOR:
		  if (pin_count() < 2) {
		      cerr << get_fileline() << ": error: the XNOR "
		              "primitive must have an input." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::XNOR, instance_width);
		  break;
		case XOR:
		  if (pin_count() < 2) {
		      cerr << get_fileline() << ": error: the XOR "
		              "primitive must have an input." << endl;
		      des->errors += 1;
		      return;
		  } else
		      cur[idx] = new NetLogic(scope, inm, pin_count(),
					      NetLogic::XOR, instance_width);
		  break;
		case TRAN:
		  if (pin_count() != 2) {
			cerr << get_fileline() << ": error: Pin count for "
			     << "tran device." << endl;
			des->errors += 1;
			return;
		  } else {
			cur[idx] = new NetTran(scope, inm, IVL_SW_TRAN);
		  }
		  break;
		case RTRAN:
		  if (pin_count() != 2) {
			cerr << get_fileline() << ": error: Pin count for "
			     << "rtran device." << endl;
			des->errors += 1;
			return;
		  } else {
			cur[idx] = new NetTran(scope, inm, IVL_SW_RTRAN);
			return;
		  }
		  break;
		case TRANIF0:
		  if (pin_count() != 3) {
			cerr << get_fileline() << ": error: Pin count for "
			     << "tranif0 device." << endl;
			des->errors += 1;
			return;
		  } else {
			cur[idx] = new NetTran(scope, inm, IVL_SW_TRANIF0);
		  }
		  break;
		case RTRANIF0:
		  if (pin_count() != 3) {
			cerr << get_fileline() << ": error: Pin count for "
			     << "rtranif0 device." << endl;
			des->errors += 1;
			return;
		  } else {
			cur[idx] = new NetTran(scope, inm, IVL_SW_RTRANIF0);
		  }
		  break;
		case TRANIF1:
		  if (pin_count() != 3) {
			cerr << get_fileline() << ": error: Pin count for "
			     << "tranif1 device." << endl;
			des->errors += 1;
			return;
		  } else {
			cur[idx] = new NetTran(scope, inm, IVL_SW_TRANIF1);
		  }
		  break;
		case RTRANIF1:
		  if (pin_count() != 3) {
			cerr << get_fileline() << ": error: Pin count for "
			     << "rtranif1 device." << endl;
			des->errors += 1;
			return;
		  } else {
			cur[idx] = new NetTran(scope, inm, IVL_SW_RTRANIF1);
		  }
		  break;
		default:
		  cerr << get_fileline() << ": internal error: unhandled "
			"gate type." << endl;
		  des->errors += 1;
		  return;
	    }

	    for (unsigned adx = 0 ;  adx < attrib_list_n ;  adx += 1)
		  cur[idx]->attribute(attrib_list[adx].key,
				      attrib_list[adx].val);

	      /* The logic devices have some uniform processing. Then
	         all may have output delays and output drive strength. */
	    if (NetLogic*log = dynamic_cast<NetLogic*> (cur[idx])) {
		  log->rise_time(rise_time);
		  log->fall_time(fall_time);
		  log->decay_time(decay_time);

		  log->pin(0).drive0(drive_type(strength0()));
		  log->pin(0).drive1(drive_type(strength1()));
	    }

	    cur[idx]->set_line(*this);
	    des->add_node(cur[idx]);
      }


      delete[]attrib_list;

	/* The gates have all been allocated, this loop runs through
	   the parameters and attaches the ports of the objects. */

      for (unsigned idx = 0 ;  idx < pin_count() ;  idx += 1) {
	    const PExpr*ex = pin(idx);
	    if (ex == 0) {
		  cerr << get_fileline() << ": error: Logic gate port "
		          "expressions are not optional." << endl;
		  des->errors += 1;
		  return;
	    }
	    NetNet*sig = (idx == 0)
		  ? lval_sig
		  : ex->elaborate_net(des, scope, 0, 0, 0, 0);
	    if (sig == 0)
		  continue;

	    assert(sig);

	    if (count == 1) {
		    /* Handle the case where there is one gate that
		       carries the whole vector width. */

		  if (1 == sig->vector_width() && instance_width != 1) {

			assert(sig->vector_width() == 1);
			NetReplicate*rep
			      = new NetReplicate(scope,
						 scope->local_symbol(),
						 instance_width,
						 instance_width);
			rep->set_line(*this);
			des->add_node(rep);
			connect(rep->pin(1), sig->pin(0));

			sig = new NetNet(scope, scope->local_symbol(),
					 NetNet::WIRE, instance_width);
			sig->data_type(IVL_VT_LOGIC);
			sig->local_flag(true);
			sig->set_line(*this);
			connect(rep->pin(0), sig->pin(0));

		  }

		  if (instance_width != sig->vector_width()) {

			cerr << get_fileline() << ": error: "
			     << "Expression width " << sig->vector_width()
			     << " does not match width " << instance_width
			     << " of logic gate array port " << idx
			     << "." << endl;
			des->errors += 1;
		  }

		  connect(cur[0]->pin(idx), sig->pin(0));

	    } else if (sig->vector_width() == 1) {
		    /* Handle the case where a single bit is connected
		       repetitively to all the instances. */
		  for (unsigned gdx = 0 ;  gdx < count ;  gdx += 1)
			connect(cur[gdx]->pin(idx), sig->pin(0));

	    } else if (sig->vector_width() == count) {

		    /* Handle the general case that each bit of the
		       value is connected to a different instance. In
		       this case, the output is handled slightly
		       different from the inputs. */
		  if (idx == 0) {
			NetConcat*cc = new NetConcat(scope,
						     scope->local_symbol(),
						     sig->vector_width(),
						     count);
			des->add_node(cc);

			  /* Connect the concat to the signal. */
			connect(cc->pin(0), sig->pin(0));

			  /* Connect the outputs of the gates to the concat. */
			for (unsigned gdx = 0 ;  gdx < count ;  gdx += 1) {
			      connect(cur[gdx]->pin(0), cc->pin(gdx+1));

			      NetNet*tmp2 = new NetNet(scope,
						       scope->local_symbol(),
						       NetNet::WIRE, 1);
			      tmp2->local_flag(true);
			      tmp2->data_type(IVL_VT_LOGIC);
			      connect(cc->pin(gdx+1), tmp2->pin(0));
			}

		  } else for (unsigned gdx = 0 ;  gdx < count ;  gdx += 1) {
			  /* Use part selects to get the bits
			     connected to the inputs of out gate. */
			NetPartSelect*tmp1 = new NetPartSelect(sig, gdx, 1,
							   NetPartSelect::VP);
			tmp1->set_line(*this);
			des->add_node(tmp1);
			connect(tmp1->pin(1), sig->pin(0));
			NetNet*tmp2 = new NetNet(scope, scope->local_symbol(),
						 NetNet::WIRE, 1);
			tmp2->local_flag(true);
			tmp2->data_type(sig->data_type());
			connect(tmp1->pin(0), tmp2->pin(0));
			connect(cur[gdx]->pin(idx), tmp1->pin(0));
		  }

	    } else {
		  cerr << get_fileline() << ": error: Gate count of " <<
			count << " does not match net width of " <<
			sig->vector_width() << " at pin " << idx << "."
		       << endl;
		  des->errors += 1;
	    }

      }

	// "cur" is an array of pointers, and we don't need it any more.
      delete[]cur;
}

NetNet*PGModule::resize_net_to_port_(Design*des, NetScope*scope,
				     NetNet*sig, unsigned port_wid,
				     NetNet::PortType dir) const
{
      ivl_assert(*this, dir != NetNet::NOT_A_PORT);
      ivl_assert(*this, dir != NetNet::PIMPLICIT);

      NetNet*tmp = new NetNet(scope, scope->local_symbol(),
			      NetNet::WIRE, port_wid);
      tmp->local_flag(true);
      tmp->set_line(*sig);

	// Handle the special case of a bi-directional part
	// select. Create a NetTran(VP) instead of a uni-directional
	// NetPartSelect node.
      if (dir == NetNet::PINOUT) {
	    unsigned wida = sig->vector_width();
	    unsigned widb = tmp->vector_width();
	    bool part_b = widb < wida;
	    NetTran*node = new NetTran(scope, scope->local_symbol(),
				       part_b? wida : widb,
				       part_b? widb : wida,
				       0);
	    if (part_b) {
		  connect(node->pin(0), sig->pin(0));
		  connect(node->pin(1), tmp->pin(0));
	    } else {
		  connect(node->pin(0), tmp->pin(0));
		  connect(node->pin(1), sig->pin(0));
	    }

	    node->set_line(*this);
	    des->add_node(node);

	    return tmp;
      }

      NetPartSelect*node = 0;

      switch (dir) {
	  case NetNet::POUTPUT:
	    if (tmp->vector_width() > sig->vector_width()) {
		  node = new NetPartSelect(tmp, 0, sig->vector_width(),
					   NetPartSelect::VP);
		  connect(node->pin(0), sig->pin(0));
	    } else {
		  node = new NetPartSelect(sig, 0, tmp->vector_width(),
					  NetPartSelect::PV);
		  connect(node->pin(0), tmp->pin(0));
	    }
	    break;

	  case NetNet::PINPUT:
	    if (tmp->vector_width() > sig->vector_width()) {
		  node = new NetPartSelect(tmp, 0, sig->vector_width(),
					   NetPartSelect::PV);
		  connect(node->pin(0), sig->pin(0));
	    } else {
		  node = new NetPartSelect(sig, 0, tmp->vector_width(),
					   NetPartSelect::VP);
		  connect(node->pin(0), tmp->pin(0));
	    }
	    break;

	  case NetNet::PINOUT:
	    ivl_assert(*this, 0);
	    break;

	  default:
	    ivl_assert(*this, 0);
      }

      des->add_node(node);

      return tmp;
}

/*
 * Instantiate a module by recursively elaborating it. Set the path of
 * the recursive elaboration so that signal names get properly
 * set. Connect the ports of the instantiated module to the signals of
 * the parameters. This is done with BUFZ gates so that they look just
 * like continuous assignment connections.
 */
void PGModule::elaborate_mod_(Design*des, Module*rmod, NetScope*scope) const
{

      assert(scope);

      if (debug_elaborate) {
	    cerr << get_fileline() << ": debug: Instantiate module "
		 << rmod->mod_name() << " with instance name "
		 << get_name() << " in scope " << scope_path(scope) << endl;
      }

	// This is the array of pin expressions, shuffled to match the
	// order of the declaration. If the source instantiation uses
	// bind by order, this is the same as the source list.Otherwise,
	// the source list is rearranged by name binding into this list.
      svector<PExpr*>pins (rmod->port_count());

	// If the instance has a pins_ member, then we know we are
	// binding by name. Therefore, make up a pins array that
	// reflects the positions of the named ports.
      if (pins_) {
	    unsigned nexp = rmod->port_count();

	      // Scan the bindings, matching them with port names.
	    for (unsigned idx = 0 ;  idx < npins_ ;  idx += 1) {

		    // Given a binding, look at the module port names
		    // for the position that matches the binding name.
		  unsigned pidx = rmod->find_port(pins_[idx].name);

		    // If the port name doesn't exist, the find_port
		    // method will return the port count. Detect that
		    // as an error.
		  if (pidx == nexp) {
			cerr << get_fileline() << ": error: port ``" <<
			      pins_[idx].name << "'' is not a port of "
			     << get_name() << "." << endl;
			des->errors += 1;
			continue;
		  }

		    // If I already bound something to this port, then
		    // the pins array will already have a pointer
		    // value where I want to place this expression.
		  if (pins[pidx]) {
			cerr << get_fileline() << ": error: port ``" <<
			      pins_[idx].name << "'' already bound." <<
			      endl;
			des->errors += 1;
			continue;
		  }

		    // OK, do the binding by placing the expression in
		    // the right place.
		  pins[pidx] = pins_[idx].parm;
	    }


      } else if (pin_count() == 0) {

	      /* Handle the special case that no ports are
		 connected. It is possible that this is an empty
		 connect-by-name list, so we'll allow it and assume
		 that is the case. */

	    for (unsigned idx = 0 ;  idx < rmod->port_count() ;  idx += 1)
		  pins[idx] = 0;

      } else {

	      /* Otherwise, this is a positional list of port
		 connections. In this case, the port count must be
		 right. Check that is is, the get the pin list. */

	    if (pin_count() != rmod->port_count()) {
		  cerr << get_fileline() << ": error: Wrong number "
			"of ports. Expecting " << rmod->port_count() <<
			", got " << pin_count() << "."
		       << endl;
		  des->errors += 1;
		  return;
	    }

	      // No named bindings, just use the positional list I
	      // already have.
	    assert(pin_count() == rmod->port_count());
	    pins = get_pins();
      }

	// Elaborate these instances of the module. The recursive
	// elaboration causes the module to generate a netlist with
	// the ports represented by NetNet objects. I will find them
	// later.

      NetScope::scope_vec_t&instance = scope->instance_arrays[get_name()];
      for (unsigned inst = 0 ;  inst < instance.count() ;  inst += 1) {
	    rmod->elaborate(des, instance[inst]);
      }



	// Now connect the ports of the newly elaborated designs to
	// the expressions that are the instantiation parameters. Scan
	// the pins, elaborate the expressions attached to them, and
	// bind them to the port of the elaborated module.

	// This can get rather complicated because the port can be
	// unconnected (meaning an empty parameter is passed) connected
	// to a concatenation, or connected to an internally
	// unconnected port.

      for (unsigned idx = 0 ;  idx < pins.count() ;  idx += 1) {

	      // Skip unconnected module ports. This happens when a
	      // null parameter is passed in.

	    if (pins[idx] == 0) {

		    // While we're here, look to see if this
		    // unconnected (from the outside) port is an
		    // input. If so, consider printing a port binding
		    // warning.
		  if (warn_portbinding) {
			svector<PEIdent*> mport = rmod->get_port(idx);
			if (mport.count() == 0)
			      continue;

			perm_string pname = peek_tail_name(mport[0]->path());

			NetNet*tmp = instance[0]->find_signal(pname);
			assert(tmp);

			if (tmp->port_type() == NetNet::PINPUT) {
			      cerr << get_fileline() << ": warning: "
				   << "Instantiating module "
				   << rmod->mod_name()
				   << " with dangling input port "
				   << rmod->ports[idx]->name
				   << "." << endl;
			}
		  }

		  continue;
	    }


	      // Inside the module, the port is zero or more signals
	      // that were already elaborated. List all those signals
	      // and the NetNet equivalents, for all the instances.
	    svector<PEIdent*> mport = rmod->get_port(idx);
	    svector<NetNet*>prts (mport.count() * instance.count());

	    if (debug_elaborate) {
		  cerr << get_fileline() << ": debug: " << get_name()
		       << ": Port " << idx << " has " << prts.count()
		       << " sub-ports." << endl;
	    }

	      // Count the internal vector bits of the port.
	    unsigned prts_vector_width = 0;

	    for (unsigned inst = 0 ;  inst < instance.count() ;  inst += 1) {
		    // Scan the instances from MSB to LSB. The port
		    // will be assembled in that order as well.
		  NetScope*inst_scope = instance[instance.count()-inst-1];

		    // Scan the module sub-ports for this instance...
		  for (unsigned ldx = 0 ;  ldx < mport.count() ;  ldx += 1) {
			unsigned lbase = inst * mport.count();
			PEIdent*pport = mport[ldx];
			assert(pport);
			prts[lbase + ldx]
			      = pport->elaborate_port(des, inst_scope);
			if (prts[lbase + ldx] == 0)
			      continue;

			assert(prts[lbase + ldx]);
			prts_vector_width += prts[lbase + ldx]->vector_width();
		  }
	    }

	      // If I find that the port is unconnected inside the
	      // module, then there is nothing to connect. Skip the
	      // argument.
	    if (prts_vector_width == 0) {
		  continue;
	    }

	      // We know by design that each instance has the same
	      // width port. Therefore, the prts_pin_count must be an
	      // even multiple of the instance count.
	    assert(prts_vector_width % instance.count() == 0);

	    unsigned desired_vector_width = prts_vector_width;
	    if (instance.count() != 1)
		  desired_vector_width = 0;

	      // Elaborate the expression that connects to the
	      // module[s] port. sig is the thing outside the module
	      // that connects to the port.

	    NetNet*sig;
	    if ((prts.count() == 0)
		|| (prts[0]->port_type() == NetNet::PINPUT)) {

		    /* Input to module. elaborate the expression to
		       the desired width. If this in an instance
		       array, then let the net determine its own
		       width. We use that, then, to decide how to hook
		       it up.

		       NOTE that this also handles the case that the
		       port is actually empty on the inside. We assume
		       in that case that the port is input. */

		  sig = pins[idx]->elaborate_net(des, scope,
						 desired_vector_width,
						 0, 0, 0);
		  if (sig == 0) {
			cerr << pins[idx]->get_fileline()
			     << ": internal error: Port expression "
			     << "too complicated for elaboration." << endl;
			continue;
		  }

	    } else if (prts[0]->port_type() == NetNet::PINOUT) {

		    /* Inout to/from module. This is a more
		       complicated case, where the expression must be
		       an lnet, but also an r-value net.

		       Normally, this winds up being the same as if we
		       just elaborated as an lnet, as passing a simple
		       identifier elaborates to the same NetNet in
		       both cases so the extra elaboration has no
		       effect. But if the expression passed to the
		       inout port is a part select, a special part
		       select must be created that can pass data in
		       both directions.

		       Use the elaborate_bi_net method to handle all
		       the possible cases. */

		  sig = pins[idx]->elaborate_bi_net(des, scope);
		  if (sig == 0) {
			cerr << pins[idx]->get_fileline() << ": error: "
			     << "Inout port expression must support "
			     << "continuous assignment." << endl;
			cerr << pins[idx]->get_fileline() << ":      : "
			     << "Port of " << rmod->mod_name()
			     << " is " << rmod->ports[idx]->name << endl;
			des->errors += 1;
			continue;
		  }


	    } else {

		    /* Port type must be OUTPUT here. */

		    /* Output from module. Elaborate the port
		       expression as the l-value of a continuous
		       assignment, as the port will continuous assign
		       into the port. */

		  sig = pins[idx]->elaborate_lnet(des, scope);
		  if (sig == 0) {
			cerr << pins[idx]->get_fileline() << ": error: "
			     << "Output port expression must support "
			     << "continuous assignment." << endl;
			cerr << pins[idx]->get_fileline() << ":      : "
			     << "Port of " << rmod->mod_name()
			     << " is " << rmod->ports[idx]->name << endl;
			des->errors += 1;
			continue;
		  }

	    }

	    assert(sig);

#ifndef NDEBUG
	    if ((prts.count() >= 1)
		&& (prts[0]->port_type() != NetNet::PINPUT)) {
		  assert(sig->type() != NetNet::REG);
	    }
#endif

	      /* If we are working with an instance array, then the
		 signal width must match the port width exactly. */
	    if ((instance.count() != 1)
		&& (sig->vector_width() != prts_vector_width)
		&& (sig->vector_width() != prts_vector_width/instance.count())) {
		  cerr << pins[idx]->get_fileline() << ": error: "
		       << "Port expression width " << sig->vector_width()
		       << " does not match expected width "<< prts_vector_width
		       << " or " << (prts_vector_width/instance.count())
		       << "." << endl;
		  des->errors += 1;
		  continue;
	    }

	    if (debug_elaborate) {
		  cerr << get_fileline() << ": debug: " << get_name()
		       << ": Port " << (idx+1) << " has vector width of "
		       << prts_vector_width << "." << endl;
	    }

	      // Check that the parts have matching pin counts. If
	      // not, they are different widths. Note that idx is 0
	      // based, but users count parameter positions from 1.
	    if ((instance.count() == 1)
		&& (prts_vector_width != sig->vector_width())) {
		  const char *tmp3 = rmod->ports[idx]->name.str();
		  if (tmp3 == 0) tmp3 = "???";
		  cerr << get_fileline() << ": warning: Port " << (idx+1)
		       << " (" << tmp3 << ") of "
		       << type_ << " expects " << prts_vector_width <<
			" bits, got " << sig->vector_width() << "." << endl;

		  if (prts_vector_width > sig->vector_width()) {
			cerr << get_fileline() << ":        : Leaving "
			     << (prts_vector_width-sig->vector_width())
			     << " high bits of the port unconnected."
			     << endl;


		  } else {
			cerr << get_fileline() << ":        : Leaving "
			     << (sig->vector_width()-prts_vector_width)
			     << " high bits of the expression dangling."
			     << endl;
		  }

		  sig = resize_net_to_port_(des, scope, sig, prts_vector_width,
					    prts[0]->port_type());
	    }

	      // Connect the sig expression that is the context of the
	      // module instance to the ports of the elaborated module.

	      // The prts_pin_count variable is the total width of the
	      // port and is the maximum number of connections to
	      // make. sig is the elaborated expression that connects
	      // to that port. If sig has too few pins, then reduce
	      // the number of connections to make.

	      // Connect this many of the port pins. If the expression
	      // is too small, then reduce the number of connects.
	    unsigned ccount = prts_vector_width;
	    if (instance.count() == 1 && sig->vector_width() < ccount)
		  ccount = sig->vector_width();

	      // The spin_modulus is the width of the signal (not the
	      // port) if this is an instance array. This causes
	      // signals wide enough for a single instance to be
	      // connected to all the instances.
	    unsigned spin_modulus = prts_vector_width;
	    if (instance.count() != 1)
		  spin_modulus = sig->vector_width();

	      // Now scan the concatenation that makes up the port,
	      // connecting pins until we run out of port pins or sig
	      // pins. The sig object is the NetNet that is connected
	      // to the port from the outside, and the prts object is
	      // an array of signals to be connected to the sig.

	    NetConcat*ctmp;
	    unsigned spin = 0;

	    if (prts.count() == 1) {

		    // The simplest case, there are no
		    // parts/concatenations on the inside of the
		    // module, so the port and sig need simply be
		    // connected directly.
		  connect(prts[0]->pin(0), sig->pin(0));

	    } else if (sig->vector_width()==prts_vector_width/instance.count()
		       && prts.count()/instance.count() == 1) {

		  if (debug_elaborate){
			cerr << get_fileline() << ": debug: " << get_name()
			     << ": Replicating " << prts_vector_width
			     << " bits across all "
			     << prts_vector_width/instance.count()
			     << " sub-ports." << endl;
		  }

		    // The signal width is exactly the width of a
		    // single instance of the port. In this case,
		    // connect the sig to all the ports identically.
		  for (unsigned ldx = 0 ;  ldx < prts.count() ;  ldx += 1)
			connect(prts[ldx]->pin(0), sig->pin(0));

	    } else switch (prts[0]->port_type()) {
		case NetNet::POUTPUT:
		  ctmp = new NetConcat(scope, scope->local_symbol(),
				       prts_vector_width,
				       prts.count());
		  des->add_node(ctmp);
		  connect(ctmp->pin(0), sig->pin(0));
		  for (unsigned ldx = 0 ;  ldx < prts.count() ;  ldx += 1) {
			connect(ctmp->pin(ldx+1),
				prts[prts.count()-ldx-1]->pin(0));
		  }
		  break;

		case NetNet::PINPUT:
		  if (debug_elaborate){
			cerr << get_fileline() << ": debug: " << get_name()
			     << ": Dividing " << prts_vector_width
			     << " bits across all "
			     << prts_vector_width/instance.count()
			     << " input sub-ports of port "
			     << idx << "." << endl;
		  }

		  for (unsigned ldx = 0 ;  ldx < prts.count() ;  ldx += 1) {
			NetNet*sp = prts[prts.count()-ldx-1];
			NetPartSelect*ptmp = new NetPartSelect(sig, spin,
							   sp->vector_width(),
							   NetPartSelect::VP);
			des->add_node(ptmp);
			connect(ptmp->pin(0), sp->pin(0));
			spin += sp->vector_width();
		  }
		  break;
		case NetNet::PINOUT:
		  cerr << get_fileline() << ": XXXX: "
		       << "Forgot how to bind inout ports!" << endl;
		  des->errors += 1;
		  break;
		case NetNet::PIMPLICIT:
		  cerr << get_fileline() << ": internal error: "
		       << "Unexpected IMPLICIT port" << endl;
		  des->errors += 1;
		  break;
		case NetNet::NOT_A_PORT:
		  cerr << get_fileline() << ": internal error: "
		       << "Unexpected NOT_A_PORT port." << endl;
		  des->errors += 1;
		  break;
	    }

      }

}

/*
 * From a UDP definition in the source, make a NetUDP
 * object. Elaborate the pin expressions as netlists, then connect
 * those networks to the pins.
 */

void PGModule::elaborate_udp_(Design*des, PUdp*udp, NetScope*scope) const
{
      NetExpr*rise_expr =0, *fall_expr =0, *decay_expr =0;

      perm_string my_name = get_name();
      if (my_name == 0)
	    my_name = scope->local_symbol();

	/* When the parser notices delay expressions in front of a
	   module or primitive, it interprets them as parameter
	   overrides. Correct that misconception here. */
      if (overrides_) {
	    PDelays tmp_del;
	    tmp_del.set_delays(overrides_, false);
	    tmp_del.eval_delays(des, scope, rise_expr, fall_expr, decay_expr);

	    if (dynamic_cast<NetEConst*> (rise_expr)) {

	    } else {
		  cerr << get_fileline() << ": error: Delay expressions must be "
		       << "constant for primitives." << endl;
		  cerr << get_fileline() << ":      : Cannot calculate "
		       << *rise_expr << endl;
		  des->errors += 1;
	    }

	    if (dynamic_cast<NetEConst*> (fall_expr)) {

	    } else {
		  cerr << get_fileline() << ": error: Delay expressions must be "
		       << "constant for primitives." << endl;
		  cerr << get_fileline() << ":      : Cannot calculate "
		       << *rise_expr << endl;
		  des->errors += 1;
	    }

	    if (dynamic_cast<NetEConst*> (decay_expr)) {

	    } else {
		  cerr << get_fileline() << ": error: Delay expressions must be "
		       << "constant for primitives." << endl;
		  cerr << get_fileline() << ":      : Cannot calculate "
		       << *rise_expr << endl;
		  des->errors += 1;
	    }

      }


      assert(udp);
      NetUDP*net = new NetUDP(scope, my_name, udp->ports.count(), udp);
      net->rise_time(rise_expr);
      net->fall_time(fall_expr);
      net->decay_time(decay_expr);

      struct attrib_list_t*attrib_list = 0;
      unsigned attrib_list_n = 0;
      attrib_list = evaluate_attributes(attributes, attrib_list_n,
					des, scope);

      for (unsigned adx = 0 ;  adx < attrib_list_n ;  adx += 1)
	    net->attribute(attrib_list[adx].key, attrib_list[adx].val);

      delete[]attrib_list;


	// This is the array of pin expressions, shuffled to match the
	// order of the declaration. If the source instantiation uses
	// bind by order, this is the same as the source
	// list. Otherwise, the source list is rearranged by name
	// binding into this list.
      svector<PExpr*>pins;

	// Detect binding by name. If I am binding by name, then make
	// up a pins array that reflects the positions of the named
	// ports. If this is simply positional binding in the first
	// place, then get the binding from the base class.
      if (pins_) {
	    unsigned nexp = udp->ports.count();
	    pins = svector<PExpr*>(nexp);

	      // Scan the bindings, matching them with port names.
	    for (unsigned idx = 0 ;  idx < npins_ ;  idx += 1) {

		    // Given a binding, look at the module port names
		    // for the position that matches the binding name.
		  unsigned pidx = udp->find_port(pins_[idx].name);

		    // If the port name doesn't exist, the find_port
		    // method will return the port count. Detect that
		    // as an error.
		  if (pidx == nexp) {
			cerr << get_fileline() << ": error: port ``" <<
			      pins_[idx].name << "'' is not a port of "
			     << get_name() << "." << endl;
			des->errors += 1;
			continue;
		  }

		    // If I already bound something to this port, then
		    // the (*exp) array will already have a pointer
		    // value where I want to place this expression.
		  if (pins[pidx]) {
			cerr << get_fileline() << ": error: port ``" <<
			      pins_[idx].name << "'' already bound." <<
			      endl;
			des->errors += 1;
			continue;
		  }

		    // OK, do the binding by placing the expression in
		    // the right place.
		  pins[pidx] = pins_[idx].parm;
	    }

      } else {

	      /* Otherwise, this is a positional list of port
		 connections. In this case, the port count must be
		 right. Check that is is, the get the pin list. */

	    if (pin_count() != udp->ports.count()) {
		  cerr << get_fileline() << ": error: Wrong number "
			"of ports. Expecting " << udp->ports.count() <<
			", got " << pin_count() << "."
		       << endl;
		  des->errors += 1;
		  return;
	    }

	      // No named bindings, just use the positional list I
	      // already have.
	    assert(pin_count() == udp->ports.count());
	    pins = get_pins();
      }


	/* Handle the output port of the primitive special. It is an
	   output port (the only output port) so must be passed an
	   l-value net. */
      if (pins[0] == 0) {
	    cerr << get_fileline() << ": warning: output port unconnected."
		 << endl;

      } else {
	    NetNet*sig = pins[0]->elaborate_lnet(des, scope);
	    if (sig == 0) {
		  cerr << get_fileline() << ": error: "
		       << "Output port expression is not valid." << endl;
		  cerr << get_fileline() << ":      : Output "
		       << "port of " << udp->name_
		       << " is " << udp->ports[0] << "." << endl;
		  des->errors += 1;
	    } else {
		  connect(sig->pin(0), net->pin(0));
	    }
      }

	/* Run through the pins, making netlists for the pin
	   expressions and connecting them to the pin in question. All
	   of this is independent of the nature of the UDP. */
      for (unsigned idx = 1 ;  idx < net->pin_count() ;  idx += 1) {
	    if (pins[idx] == 0)
		  continue;

	    NetNet*sig = pins[idx]->elaborate_net(des, scope, 1, 0, 0, 0);
	    if (sig == 0) {
		  cerr << "internal error: Expression too complicated "
			"for elaboration:" << pins[idx] << endl;
		  continue;
	    }

	    connect(sig->pin(0), net->pin(idx));
      }

	// All done. Add the object to the design.
      des->add_node(net);
}


bool PGModule::elaborate_sig(Design*des, NetScope*scope) const
{
	// Look for the module type
      map<perm_string,Module*>::const_iterator mod = pform_modules.find(type_);
      if (mod != pform_modules.end())
	    return elaborate_sig_mod_(des, scope, (*mod).second);

      map<perm_string,PUdp*>::const_iterator udp = pform_primitives.find(type_);
      if (udp != pform_primitives.end())
	    return elaborate_sig_udp_(des, scope, (*udp).second);

      return true;
}


void PGModule::elaborate(Design*des, NetScope*scope) const
{
	// Look for the module type
      map<perm_string,Module*>::const_iterator mod = pform_modules.find(type_);
      if (mod != pform_modules.end()) {
	    elaborate_mod_(des, (*mod).second, scope);
	    return;
      }

	// Try a primitive type
      map<perm_string,PUdp*>::const_iterator udp = pform_primitives.find(type_);
      if (udp != pform_primitives.end()) {
	    assert((*udp).second);
	    elaborate_udp_(des, (*udp).second, scope);
	    return;
      }

      cerr << get_fileline() << ": internal error: Unknown module type: " <<
	    type_ << endl;
}

void PGModule::elaborate_scope(Design*des, NetScope*sc) const
{
	// Look for the module type
      map<perm_string,Module*>::const_iterator mod = pform_modules.find(type_);
      if (mod != pform_modules.end()) {
	    elaborate_scope_mod_(des, (*mod).second, sc);
	    return;
      }

	// Try a primitive type
      map<perm_string,PUdp*>::const_iterator udp = pform_primitives.find(type_);
      if (udp != pform_primitives.end())
	    return;

	// Not a module or primitive that I know about yet, so try to
	// load a library module file (which parses some new Verilog
	// code) and try again.
      if (load_module(type_)) {

	      // Try again to find the module type
	    mod = pform_modules.find(type_);
	    if (mod != pform_modules.end()) {
		  elaborate_scope_mod_(des, (*mod).second, sc);
		  return;
	    }

	      // Try again to find a primitive type
	    udp = pform_primitives.find(type_);
	    if (udp != pform_primitives.end())
		  return;
      }


	// Not a module or primitive that I know about or can find by
	// any means, so give up.
      cerr << get_fileline() << ": error: Unknown module type: " << type_ << endl;
      missing_modules[type_] += 1;
      des->errors += 1;
}


NetProc* Statement::elaborate(Design*des, NetScope*) const
{
      cerr << get_fileline() << ": internal error: elaborate: "
	    "What kind of statement? " << typeid(*this).name() << endl;
      NetProc*cur = new NetProc;
      des->errors += 1;
      return cur;
}


NetAssign_* PAssign_::elaborate_lval(Design*des, NetScope*scope) const
{
      assert(lval_);
      return lval_->elaborate_lval(des, scope, false);
}

/*
 * This function elaborates delay expressions. This is a little
 * different from normal elaboration because the result may need to be
 * scaled.
 */
static NetExpr*elaborate_delay_expr(PExpr*expr, Design*des, NetScope*scope)
{
      NetExpr*dex = elab_and_eval(des, scope, expr, -1);

	/* If the delay expression is a real constant or vector
	   constant, then evaluate it, scale it to the local time
	   units, and return an adjusted NetEConst. */

      if (NetECReal*tmp = dynamic_cast<NetECReal*>(dex)) {
	    verireal fn = tmp->value();

	    int shift = scope->time_unit() - des->get_precision();
	    int64_t delay = fn.as_long64(shift);
	    if (delay < 0)
		  delay = 0;

	    delete tmp;
	    return new NetEConst(verinum(delay));
      }


      if (NetEConst*tmp = dynamic_cast<NetEConst*>(dex)) {
	    verinum fn = tmp->value();

	    uint64_t delay =
		  des->scale_to_precision(fn.as_ulong64(), scope);

	    delete tmp;
	    return new NetEConst(verinum(delay));
      }


	/* The expression is not constant, so generate an expanded
	   expression that includes the necessary scale shifts, and
	   return that expression. */
      int shift = scope->time_unit() - des->get_precision();
      if (shift > 0) {
	    uint64_t scale = 1;
	    while (shift > 0) {
		  scale *= 10;
		  shift -= 1;
	    }

	    NetExpr*scal_val = new NetEConst(verinum(scale));
	    dex = new NetEBMult('*', dex, scal_val);
      }

      if (shift < 0) {
	    unsigned long scale = 1;
	    while (shift < 0) {
		  scale *= 10;
		  shift += 1;
	    }

	    NetExpr*scal_val = new NetEConst(verinum(scale));
	    dex = new NetEBDiv('/', dex, scal_val);
      }

      return dex;
}

NetProc* PAssign::elaborate(Design*des, NetScope*scope) const
{
      assert(scope);

	/* elaborate the lval. This detects any part selects and mux
	   expressions that might exist. */
      NetAssign_*lv = elaborate_lval(des, scope);
      if (lv == 0) return 0;

	/* If there is an internal delay expression, elaborate it. */
      NetExpr*delay = 0;
      if (delay_ != 0)
	    delay = elaborate_delay_expr(delay_, des, scope);


      assert(rval());

	/* Elaborate the r-value expression, then try to evaluate it. */

	/* Find out what the r-value width is going to be. We guess it
	   will be the l-value width, but it may turn out to be
	   something else based on self-determined widths inside. */
      unsigned use_width = lv->lwidth();
      bool unsized_flag = false;
      use_width = rval()->test_width(des, scope, use_width, use_width, unsized_flag);

	/* Now elaborate to the expected width. Pass the lwidth to
	   prune any constant result to fit with the lvalue at hand. */
      NetExpr*rv = elab_and_eval(des, scope, rval(), use_width, lv->lwidth());
      if (rv == 0) return 0;
      assert(rv);


	/* Rewrite delayed assignments as assignments that are
	   delayed. For example, a = #<d> b; becomes:

	     begin
	        tmp = b;
		#<d> a = tmp;
	     end

	   If the delay is an event delay, then the transform is
	   similar, with the event delay replacing the time delay. It
	   is an event delay if the event_ member has a value.

	   This rewriting of the expression allows me to not bother to
	   actually and literally represent the delayed assign in the
	   netlist. The compound statement is exactly equivalent. */

      if (delay || event_) {
	    unsigned wid = count_lval_width(lv);

	    rv->set_width(wid);
	    rv = pad_to_width(rv, wid);

	    if (wid > rv->expr_width()) {
		  cerr << get_fileline() << ": error: Unable to match "
			"expression width of " << rv->expr_width() <<
			" to l-value width of " << wid << "." << endl;
		    //XXXX delete rv;
		  return 0;
	    }

	    NetNet*tmp = new NetNet(scope, scope->local_symbol(),
				    NetNet::REG, wid);
	    tmp->local_flag(true);
	    tmp->set_line(*this);
	    tmp->data_type(rv->expr_type());

	    NetESignal*sig = new NetESignal(tmp);

	      /* Generate an assignment of the l-value to the temporary... */
	    NetAssign_*lvt = new NetAssign_(tmp);

	    NetAssign*a1 = new NetAssign(lvt, rv);
	    a1->set_line(*this);

	      /* Generate an assignment of the temporary to the r-value... */
	    NetAssign*a2 = new NetAssign(lv, sig);
	    a2->set_line(*this);

	      /* Generate the delay statement with the final
		 assignment attached to it. If this is an event delay,
		 elaborate the PEventStatement. Otherwise, create the
		 right NetPDelay object. */
	    NetProc*st;
	    if (event_) {
		  st = event_->elaborate_st(des, scope, a2);
		  if (st == 0) {
			cerr << event_->get_fileline() << ": error: "
			      "unable to elaborate event expression."
			     << endl;
			des->errors += 1;
			return 0;
		  }
		  assert(st);

	    } else {
		  NetPDelay*de = new NetPDelay(delay, a2);
		  de->set_line(*this);
		  st = de;
	    }

	      /* And build up the complex statement. */
	    NetBlock*bl = new NetBlock(NetBlock::SEQU, 0);
	    bl->append(a1);
	    bl->append(st);

	    return bl;
      }

	/* Based on the specific type of the l-value, do cleanup
	   processing on the r-value. */
      if (rv->expr_type() == IVL_VT_REAL) {

	      // The r-value is a real. Casting will happen in the
	      // code generator, so leave it.

      } else {
	    unsigned wid = count_lval_width(lv);
	    rv->set_width(wid);
	    rv = pad_to_width(rv, wid);
	    assert(rv->expr_width() >= wid);
      }

      NetAssign*cur = new NetAssign(lv, rv);
      cur->set_line(*this);

      return cur;
}

/*
 * Elaborate non-blocking assignments. The statement is of the general
 * form:
 *
 *    <lval> <= #<delay> <rval> ;
 */
NetProc* PAssignNB::elaborate(Design*des, NetScope*scope) const
{
      assert(scope);

      if (scope->in_func()) {
	    cerr << get_fileline() << ": error: functions cannot have non "
	            "blocking assignment statements." << endl;
	    des->errors += 1;
	    return 0;
      }

	/* Elaborate the l-value. */
      NetAssign_*lv = elaborate_lval(des, scope);
      if (lv == 0) return 0;

      assert(rval());

	/* Elaborate and precalculate the r-value. */
      NetExpr*rv = elab_and_eval(des, scope, rval(), count_lval_width(lv));
      if (rv == 0)
	    return 0;

	/* Handle the (common) case that the r-value is a vector. This
	   includes just about everything but reals. In this case, we
	   need to pad the r-value to match the width of the l-value.

	   If in this case the l-val is a variable (i.e. real) then
	   the width to pad to will be 0, so this code is harmless. */
      if (rv->expr_type() == IVL_VT_REAL) {

      } else {
	    unsigned wid = count_lval_width(lv);
	    rv->set_width(wid);
	    rv = pad_to_width(rv, wid);
      }

      NetExpr*delay = 0;
      if (delay_ != 0)
	    delay = elaborate_delay_expr(delay_, des, scope);

	/* All done with this node. Mark its line number and check it in. */
      NetAssignNB*cur = new NetAssignNB(lv, rv);
      cur->set_delay(delay);
      cur->set_line(*this);
      return cur;
}


/*
 * This is the elaboration method for a begin-end block. Try to
 * elaborate the entire block, even if it fails somewhere. This way I
 * get all the error messages out of it. Then, if I detected a failure
 * then pass the failure up.
 */
NetProc* PBlock::elaborate(Design*des, NetScope*scope) const
{
      assert(scope);

      NetBlock::Type type = (bl_type_==PBlock::BL_PAR)
	    ? NetBlock::PARA
	    : NetBlock::SEQU;

      NetScope*nscope = 0;
      if (pscope_name() != 0) {
	    nscope = scope->child(hname_t(pscope_name()));
	    if (nscope == 0) {
		  cerr << get_fileline() << ": internal error: "
			"unable to find block scope " << scope_path(scope)
		       << "." << pscope_name() << endl;
		  des->errors += 1;
		  return 0;
	    }

	    assert(nscope);

	    elaborate_behaviors_(des, nscope);
      }

      NetBlock*cur = new NetBlock(type, nscope);

      if (nscope == 0)
	    nscope = scope;

	// Handle the special case that the block contains only one
	// statement. There is no need to keep the block node. Also,
	// don't elide named blocks, because they might be referenced
	// elsewhere.
      if ((list_.count() == 1) && (pscope_name() == 0)) {
	    assert(list_[0]);
	    NetProc*tmp = list_[0]->elaborate(des, nscope);
	    return tmp;
      }

      for (unsigned idx = 0 ;  idx < list_.count() ;  idx += 1) {
	    assert(list_[idx]);
	    NetProc*tmp = list_[idx]->elaborate(des, nscope);
	      // If the statement fails to elaborate, then simply
	      // ignore it. Presumably, the elaborate for the
	      // statement already generated an error message and
	      // marked the error count in the design so no need to
	      // do any of that here.
	    if (tmp == 0) {
		  continue;
	    }

	      // If the result turns out to be a noop, then skip it.
	    if (NetBlock*tbl = dynamic_cast<NetBlock*>(tmp))
		  if (tbl->proc_first() == 0) {
			delete tbl;
			continue;
		  }

	    cur->append(tmp);
      }

      return cur;
}

/*
 * Elaborate a case statement.
 */
NetProc* PCase::elaborate(Design*des, NetScope*scope) const
{
      assert(scope);

      NetExpr*expr = elab_and_eval(des, scope, expr_, -1);
      if (expr == 0) {
	    cerr << get_fileline() << ": error: Unable to elaborate this case"
		  " expression." << endl;
	    return 0;
      }

	/* Count the items in the case statement. Note that there may
	   be some cases that have multiple guards. Count each as a
	   separate item. */
      unsigned icount = 0;
      for (unsigned idx = 0 ;  idx < items_->count() ;  idx += 1) {
	    PCase::Item*cur = (*items_)[idx];

	    if (cur->expr.count() == 0)
		  icount += 1;
	    else
		  icount += cur->expr.count();
      }

      NetCase*res = new NetCase(type_, expr, icount);
      res->set_line(*this);

	/* Iterate over all the case items (guard/statement pairs)
	   elaborating them. If the guard has no expression, then this
	   is a "default" cause. Otherwise, the guard has one or more
	   expressions, and each guard is a case. */
      unsigned inum = 0;
      for (unsigned idx = 0 ;  idx < items_->count() ;  idx += 1) {

	    assert(inum < icount);
	    PCase::Item*cur = (*items_)[idx];

	    if (cur->expr.count() == 0) {
		    /* If there are no expressions, then this is the
		       default case. */
		  NetProc*st = 0;
		  if (cur->stat)
			st = cur->stat->elaborate(des, scope);

		  res->set_case(inum, 0, st);
		  inum += 1;

	    } else for (unsigned e = 0; e < cur->expr.count(); e += 1) {

		    /* If there are one or more expressions, then
		       iterate over the guard expressions, elaborating
		       a separate case for each. (Yes, the statement
		       will be elaborated again for each.) */
		  NetExpr*gu = 0;
		  NetProc*st = 0;
		  assert(cur->expr[e]);
		  gu = elab_and_eval(des, scope, cur->expr[e], -1);

		  if (cur->stat)
			st = cur->stat->elaborate(des, scope);

		  res->set_case(inum, gu, st);
		  inum += 1;
	    }
      }

      return res;
}

NetProc* PCondit::elaborate(Design*des, NetScope*scope) const
{
      assert(scope);

      if (debug_elaborate)
	    cerr << get_fileline() << ": debug: Elaborate condition statement"
		 << " with conditional: " << *expr_ << endl;

	// Elaborate and try to evaluate the conditional expression.
      NetExpr*expr = elab_and_eval(des, scope, expr_, -1);
      if (expr == 0) {
	    cerr << get_fileline() << ": error: Unable to elaborate"
		  " condition expression." << endl;
	    des->errors += 1;
	    return 0;
      }

	// If the condition of the conditional statement is constant,
	// then look at the value and elaborate either the if statement
	// or the else statement. I don't need both. If there is no
	// else_ statement, the use an empty block as a noop.
      if (NetEConst*ce = dynamic_cast<NetEConst*>(expr)) {
	    verinum val = ce->value();
	    if (debug_elaborate) {
		  cerr << get_fileline() << ": debug: Condition expression "
		       << "is a constant " << val << "." << endl;
	    }

	    verinum::V reduced = verinum::V0;
	    for (unsigned idx = 0 ;  idx < val.len() ;  idx += 1)
		  reduced = reduced | val[idx];

	    delete expr;
	    if (reduced == verinum::V1)
		  if (if_) {
			return if_->elaborate(des, scope);
		  } else {
			NetBlock*tmp = new NetBlock(NetBlock::SEQU, 0);
			tmp->set_line(*this);
			return tmp;
		  }
	    else if (else_)
		  return else_->elaborate(des, scope);
	    else
		  return new NetBlock(NetBlock::SEQU, 0);
      }

	// If the condition expression is more than 1 bits, then
	// generate a comparison operator to get the result down to
	// one bit. Turn <e> into <e> != 0;

      if (expr->expr_width() < 1) {
	    cerr << get_fileline() << ": internal error: "
		  "incomprehensible expression width (0)." << endl;
	    return 0;
      }

	// Make sure the condition expression evaluates to a condition.
      expr = condition_reduce(expr);

	// Well, I actually need to generate code to handle the
	// conditional, so elaborate.
      NetProc*i = if_? if_->elaborate(des, scope) : 0;
      NetProc*e = else_? else_->elaborate(des, scope) : 0;

	// Detect the special cases that the if or else statements are
	// empty blocks. If this is the case, remove the blocks as
	// null statements.
      if (NetBlock*tmp = dynamic_cast<NetBlock*>(i)) {
	    if (tmp->proc_first() == 0) {
		  delete i;
		  i = 0;
	    }
      }

      if (NetBlock*tmp = dynamic_cast<NetBlock*>(e)) {
	    if (tmp->proc_first() == 0) {
		  delete e;
		  e = 0;
	    }
      }

      NetCondit*res = new NetCondit(expr, i, e);
      res->set_line(*this);
      return res;
}

NetProc* PCallTask::elaborate(Design*des, NetScope*scope) const
{
      if (peek_tail_name(path_)[0] == '$')
	    return elaborate_sys(des, scope);
      else
	    return elaborate_usr(des, scope);
}

/*
 * A call to a system task involves elaborating all the parameters,
 * then passing the list to the NetSTask object.
 *XXXX
 * There is a single special case in the call to a system
 * task. Normally, an expression cannot take an unindexed
 * memory. However, it is possible to take a system task parameter a
 * memory if the expression is trivial.
 */
NetProc* PCallTask::elaborate_sys(Design*des, NetScope*scope) const
{
      assert(scope);

      unsigned parm_count = nparms();

	/* Catch the special case that the system task has no
	   parameters. The "()" string will be parsed as a single
	   empty parameter, when we really mean no parameters at all. */
      if ((nparms() == 1) && (parm(0) == 0))
	    parm_count = 0;

      svector<NetExpr*>eparms (parm_count);

      for (unsigned idx = 0 ;  idx < parm_count ;  idx += 1) {
	    PExpr*ex = parm(idx);
	    eparms[idx] = ex? ex->elaborate_expr(des, scope, -1, true) : 0;

	      /* Attempt to pre-evaluate the parameters. It may be
		 possible to at least partially reduce the
		 expression. */
	    if (eparms[idx]) eval_expr(eparms[idx]);
      }

      NetSTask*cur = new NetSTask(peek_tail_name(path_), eparms);
      cur->set_line(*this);
      return cur;
}

/*
 * A call to a user defined task is different from a call to a system
 * task because a user task in a netlist has no parameters: the
 * assignments are done by the calling thread. For example:
 *
 *  task foo;
 *    input a;
 *    output b;
 *    [...]
 *  endtask;
 *
 *  [...] foo(x, y);
 *
 * is really:
 *
 *  task foo;
 *    reg a;
 *    reg b;
 *    [...]
 *  endtask;
 *
 *  [...]
 *  begin
 *    a = x;
 *    foo;
 *    y = b;
 *  end
 */
NetProc* PCallTask::elaborate_usr(Design*des, NetScope*scope) const
{
      assert(scope);

      if (scope->in_func()) {
	    cerr << get_fileline() << ": error: functions cannot enable/call "
	            "tasks." << endl;
	    des->errors += 1;
	    return 0;
      }

      NetScope*task = des->find_task(scope, path_);
      if (task == 0) {
	    cerr << get_fileline() << ": error: Enable of unknown task "
		 << "``" << path_ << "''." << endl;
	    des->errors += 1;
	    return 0;
      }

      assert(task);
      assert(task->type() == NetScope::TASK);
      NetTaskDef*def = task->task_def();
      if (def == 0) {
	    cerr << get_fileline() << ": internal error: task " << path_
		 << " doesn't have a definition in " << scope_path(scope)
		 << "." << endl;
	    des->errors += 1;
	    return 0;
      }
      assert(def);

      if (nparms() != def->port_count()) {
	    cerr << get_fileline() << ": error: Port count mismatch in call to ``"
		 << path_ << "''. Got " << nparms()
		 << " ports, expecting " << def->port_count() << " ports." << endl;
	    des->errors += 1;
	    return 0;
      }

      NetUTask*cur;

	/* Handle tasks with no parameters specially. There is no need
	   to make a sequential block to hold the generated code. */
      if (nparms() == 0) {
	    cur = new NetUTask(task);
	    cur->set_line(*this);
	    return cur;
      }

      NetBlock*block = new NetBlock(NetBlock::SEQU, 0);


	/* Detect the case where the definition of the task is known
	   empty. In this case, we need not bother with calls to the
	   task, all the assignments, etc. Just return a no-op. */

      if (const NetBlock*tp = dynamic_cast<const NetBlock*>(def->proc())) {
	    if (tp->proc_first() == 0)
		  return block;
      }

	/* Generate assignment statement statements for the input and
	   INOUT ports of the task. These are managed by writing
	   assignments with the task port the l-value and the passed
	   expression the r-value. We know by definition that the port
	   is a reg type, so this elaboration is pretty obvious. */

      for (unsigned idx = 0 ;  idx < nparms() ;  idx += 1) {

	    NetNet*port = def->port(idx);
	    assert(port->port_type() != NetNet::NOT_A_PORT);
	    if (port->port_type() == NetNet::POUTPUT)
		  continue;

	    NetAssign_*lv = new NetAssign_(port);
	    unsigned wid = count_lval_width(lv);

	    NetExpr*rv = elab_and_eval(des, scope, parms_[idx], wid);
	    rv->set_width(wid);
	    rv = pad_to_width(rv, wid);
	    NetAssign*pr = new NetAssign(lv, rv);
	    block->append(pr);
      }

	/* Generate the task call proper... */
      cur = new NetUTask(task);
      cur->set_line(*this);
      block->append(cur);


	/* Generate assignment statements for the output and INOUT
	   ports of the task. The l-value in this case is the
	   expression passed as a parameter, and the r-value is the
	   port to be copied out.

	   We know by definition that the r-value of this copy-out is
	   the port, which is a reg. The l-value, however, may be any
	   expression that can be a target to a procedural
	   assignment, including a memory word. */

      for (unsigned idx = 0 ;  idx < nparms() ;  idx += 1) {

	    NetNet*port = def->port(idx);

	      /* Skip input ports. */
	    assert(port->port_type() != NetNet::NOT_A_PORT);
	    if (port->port_type() == NetNet::PINPUT)
		  continue;


	      /* Elaborate an l-value version of the port expression
		 for output and inout ports. If the expression does
		 not exist then quietly skip it, but if the expression
		 is not a valid l-value print an error message. Note
		 that the elaborate_lval method already printed a
		 detailed message. */
	    NetAssign_*lv;
	    if (parms_[idx]) {
		  lv = parms_[idx]->elaborate_lval(des, scope, false);
		  if (lv == 0) {
			cerr << parms_[idx]->get_fileline() << ": error: "
			     << "I give up on task port " << (idx+1)
			     << " expression: " << *parms_[idx] << endl;
		  }
	    } else {
		  lv = 0;
	    }

	    if (lv == 0)
		  continue;

	    NetESignal*sig = new NetESignal(port);
	    NetExpr*rv = pad_to_width(sig, count_lval_width(lv));

	      /* Generate the assignment statement. */
	    NetAssign*ass = new NetAssign(lv, rv);

	    block->append(ass);
      }

      return block;
}

/*
 * Elaborate a procedural continuous assign. This really looks very
 * much like other procedural assignments, at this point, but there
 * is no delay to worry about. The code generator will take care of
 * the differences between continuous assign and normal assignments.
 */
NetCAssign* PCAssign::elaborate(Design*des, NetScope*scope) const
{
      NetCAssign*dev = 0;
      assert(scope);

      NetAssign_*lval = lval_->elaborate_lval(des, scope, false);
      if (lval == 0)
	    return 0;

      unsigned lwid = count_lval_width(lval);

      NetExpr*rexp = elab_and_eval(des, scope, expr_, lwid);
      if (rexp == 0)
	    return 0;

      rexp->set_width(lwid);
      rexp = pad_to_width(rexp, lwid);

      dev = new NetCAssign(lval, rexp);

      if (debug_elaborate) {
	    cerr << get_fileline() << ": debug: Elaborate cassign,"
		 << " lval width=" << lwid
		 << " rval width=" << rexp->expr_width()
		 << " rval=" << *rexp
		 << endl;
      }

      dev->set_line(*this);
      return dev;
}

NetDeassign* PDeassign::elaborate(Design*des, NetScope*scope) const
{
      assert(scope);

      NetAssign_*lval = lval_->elaborate_lval(des, scope, false);
      if (lval == 0)
	    return 0;

      NetDeassign*dev = new NetDeassign(lval);
      dev->set_line( *this );
      return dev;
}

/*
 * Elaborate the delay statement (of the form #<expr> <statement>) as a
 * NetPDelay object. If the expression is constant, evaluate it now
 * and make a constant delay. If not, then pass an elaborated
 * expression to the constructor of NetPDelay so that the code
 * generator knows to evaluate the expression at run time.
 */
NetProc* PDelayStatement::elaborate(Design*des, NetScope*scope) const
{
      assert(scope);

      if (scope->in_func()) {
	    cerr << get_fileline() << ": error: functions cannot have "
	            "delay statements." << endl;
	    des->errors += 1;
	    return 0;
      }

	/* This call evaluates the delay expression to a NetEConst, if
	   possible. This includes transforming NetECReal values to
	   integers, and applying the proper scaling. */
      NetExpr*dex = elaborate_delay_expr(delay_, des, scope);

      if (NetEConst*tmp = dynamic_cast<NetEConst*>(dex)) {
	    if (statement_)
		  return new NetPDelay(tmp->value().as_ulong64(),
				       statement_->elaborate(des, scope));
	    else
		  return new NetPDelay(tmp->value().as_ulong(), 0);

	    delete dex;

      } else {
	    if (statement_)
		  return new NetPDelay(dex, statement_->elaborate(des, scope));
	    else
		  return new NetPDelay(dex, 0);
      }

}

/*
 * The disable statement is not yet supported.
 */
NetProc* PDisable::elaborate(Design*des, NetScope*scope) const
{
      assert(scope);

      list<hname_t> spath = eval_scope_path(des, scope, scope_);

      NetScope*target = des->find_scope(scope, spath);
      if (target == 0) {
	    cerr << get_fileline() << ": error: Cannot find scope "
		 << scope_ << " in " << scope_path(scope) << endl;
	    des->errors += 1;
	    return 0;
      }

      switch (target->type()) {
	  case NetScope::FUNC:
	    cerr << get_fileline() << ": error: Cannot disable functions." << endl;
	    des->errors += 1;
	    return 0;

	  case NetScope::MODULE:
	    cerr << get_fileline() << ": error: Cannot disable modules." << endl;
	    des->errors += 1;
	    return 0;

	  default:
	    break;
      }

      NetDisable*obj = new NetDisable(target);
      obj->set_line(*this);
      return obj;
}

/*
 * An event statement is an event delay of some sort, attached to a
 * statement. Some Verilog examples are:
 *
 *      @(posedge CLK) $display("clock rise");
 *      @event_1 $display("event triggered.");
 *      @(data or negedge clk) $display("data or clock fall.");
 *
 * The elaborated netlist uses the NetEvent, NetEvWait and NetEvProbe
 * classes. The NetEvWait class represents the part of the netlist
 * that is executed by behavioral code. The process starts waiting on
 * the NetEvent when it executes the NetEvWait step. Net NetEvProbe
 * and NetEvTrig are structural and behavioral equivalents that
 * trigger the event, and awakens any processes blocking in the
 * associated wait.
 *
 * The basic data structure is:
 *
 *       NetEvWait ---/--->  NetEvent  <----\---- NetEvProbe
 *        ...         |                     |         ...
 *       NetEvWait ---+                     +---- NetEvProbe
 *                                          |         ...
 *                                          +---- NetEvTrig
 *
 * That is, many NetEvWait statements may wait on a single NetEvent
 * object, and Many NetEvProbe objects may trigger the NetEvent
 * object. The many NetEvWait objects pointing to the NetEvent object
 * reflects the possibility of different places in the code blocking
 * on the same named event, like so:
 *
 *         event foo;
 *           [...]
 *         always begin @foo <statement1>; @foo <statement2> end
 *
 * This tends to not happen with signal edges. The multiple probes
 * pointing to the same event reflect the possibility of many
 * expressions in the same blocking statement, like so:
 *
 *         wire reset, clk;
 *           [...]
 *         always @(reset or posedge clk) <stmt>;
 *
 * Conjunctions like this cause a NetEvent object be created to
 * represent the overall conjunction, and NetEvProbe objects for each
 * event expression.
 *
 * If the NetEvent object represents a named event from the source,
 * then there are NetEvTrig objects that represent the trigger
 * statements instead of the NetEvProbe objects representing signals.
 * For example:
 *
 *         event foo;
 *         always @foo <stmt>;
 *         initial begin
 *                [...]
 *            -> foo;
 *                [...]
 *            -> foo;
 *                [...]
 *         end
 *
 * Each trigger statement in the source generates a separate NetEvTrig
 * object in the netlist. Those trigger objects are elaborated
 * elsewhere.
 *
 * Additional complications arise when named events show up in
 * conjunctions. An example of such a case is:
 *
 *         event foo;
 *         wire bar;
 *         always @(foo or posedge bar) <stmt>;
 *
 * Since there is by definition a NetEvent object for the foo object,
 * this is handled by allowing the NetEvWait object to point to
 * multiple NetEvent objects. All the NetEvProbe based objects are
 * collected and pointed as the synthetic NetEvent object, and all the
 * named events are added into the list of NetEvent object that the
 * NetEvWait object can refer to.
 */

NetProc* PEventStatement::elaborate_st(Design*des, NetScope*scope,
				       NetProc*enet) const
{
      assert(scope);

      if (scope->in_func()) {
	    cerr << get_fileline() << ": error: functions cannot have "
	            "event statements." << endl;
	    des->errors += 1;
	    return 0;
      }

	/* Create a single NetEvent and NetEvWait. Then, create a
	   NetEvProbe for each conjunctive event in the event
	   list. The NetEvProbe objects all refer back to the NetEvent
	   object. */

      NetEvent*ev = new NetEvent(scope->local_symbol());
      ev->set_line(*this);
      unsigned expr_count = 0;

      NetEvWait*wa = new NetEvWait(enet);
      wa->set_line(*this);

	/* If there are no expressions, this is a signal that it is an
	   @* statement. Generate an expression to use. */

      if (expr_.count() == 0) {
	    assert(enet);
	     /* For synthesis we want just the inputs, but for the rest we
	      * want inputs and outputs that may cause a value to change. */
	    extern bool synthesis; /* Synthesis flag from main.cc */
	    bool rem_out = false;
	    if (synthesis) {
		  rem_out = true;
	    }
	    NexusSet*nset = enet->nex_input(rem_out);
	    if (nset == 0) {
		  cerr << get_fileline() << ": internal error: No NexusSet"
		       << " from statement." << endl;
		  enet->dump(cerr, 6);
		  des->errors += 1;
		  return enet;
	    }

	    if (nset->count() == 0) {
		  cerr << get_fileline() << ": error: No inputs to statement."
		       << " The @* cannot execute." << endl;
		  des->errors += 1;
		  return enet;
	    }

	    NetEvProbe*pr = new NetEvProbe(scope, scope->local_symbol(),
					   ev, NetEvProbe::ANYEDGE,
					   nset->count());
	    for (unsigned idx = 0 ;  idx < nset->count() ;  idx += 1)
		  connect(nset[0][idx], pr->pin(idx));

	    delete nset;
	    des->add_node(pr);

	    expr_count = 1;

      } else for (unsigned idx = 0 ;  idx < expr_.count() ;  idx += 1) {

	    assert(expr_[idx]->expr());

	      /* If the expression is an identifier that matches a
		 named event, then handle this case all at once at
		 skip the rest of the expression handling. */

	    if (PEIdent*id = dynamic_cast<PEIdent*>(expr_[idx]->expr())) {
		  NetNet*       sig = 0;
		  const NetExpr*par = 0;
		  NetEvent*     eve = 0;

		  NetScope*found_in = symbol_search(des, scope, id->path(),
						    sig, par, eve);

		  if (found_in && eve) {
			wa->add_event(eve);
			continue;
		  }
	    }


	      /* So now we have a normal event expression. Elaborate
		 the sub-expression as a net and decide how to handle
		 the edge. */

	    bool save_flag = error_implicit;
	    error_implicit = true;
	    NetNet*expr = expr_[idx]->expr()->elaborate_net(des, scope,
							    0, 0, 0, 0);
	    error_implicit = save_flag;
	    if (expr == 0) {
		  expr_[idx]->dump(cerr);
		  cerr << endl;
		  des->errors += 1;
		  continue;
	    }
	    assert(expr);

	    unsigned pins = (expr_[idx]->type() == PEEvent::ANYEDGE)
		  ? expr->pin_count() : 1;

	    NetEvProbe*pr;
	    switch (expr_[idx]->type()) {
		case PEEvent::POSEDGE:
		  pr = new NetEvProbe(scope, scope->local_symbol(), ev,
				      NetEvProbe::POSEDGE, pins);
		  break;

		case PEEvent::NEGEDGE:
		  pr = new NetEvProbe(scope, scope->local_symbol(), ev,
				      NetEvProbe::NEGEDGE, pins);
		  break;

		case PEEvent::ANYEDGE:
		  pr = new NetEvProbe(scope, scope->local_symbol(), ev,
				      NetEvProbe::ANYEDGE, pins);
		  break;

		default:
		  assert(0);
	    }

	    for (unsigned p = 0 ;  p < pr->pin_count() ; p += 1)
		  connect(pr->pin(p), expr->pin(p));

	    des->add_node(pr);
	    expr_count += 1;
      }

	/* If there was at least one conjunction that was an
	   expression (and not a named event) then add this
	   event. Otherwise, we didn't use it so delete it. */
      if (expr_count > 0) {
	    scope->add_event(ev);
	    wa->add_event(ev);
	      /* NOTE: This event that I am adding to the wait may be
		 a duplicate of another event somewhere else. However,
		 I don't know that until all the modules are hooked
		 up, so it is best to leave find_similar_event to
		 after elaboration. */
      } else {
	    delete ev;
      }

      return wa;
}

/*
 * This is the special case of the event statement, the wait
 * statement. This is elaborated into a slightly more complicated
 * statement that uses non-wait statements:
 *
 *     wait (<expr>)  <statement>
 *
 * becomes
 *
 *     begin
 *         while (1 !== <expr>)
 *           @(<expr inputs>) <noop>;
 *         <statement>;
 *     end
 */
NetProc* PEventStatement::elaborate_wait(Design*des, NetScope*scope,
					 NetProc*enet) const
{
      assert(scope);
      assert(expr_.count() == 1);

      if (scope->in_func()) {
	    cerr << get_fileline() << ": error: functions cannot have "
	            "wait statements." << endl;
	    des->errors += 1;
	    return 0;
      }

      const PExpr *pe = expr_[0]->expr();

	/* Elaborate wait expression. Don't eval yet, we will do that
	   shortly, after we apply a reduction or. */
      NetExpr*expr = pe->elaborate_expr(des, scope, -1, false);
      if (expr == 0) {
	    cerr << get_fileline() << ": error: Unable to elaborate"
		  " wait condition expression." << endl;
	    des->errors += 1;
	    return 0;
      }

	// If the condition expression is more than 1 bits, then
	// generate a reduction operator to get the result down to
	// one bit. In other words, Turn <e> into |<e>;

      if (expr->expr_width() < 1) {
	    cerr << get_fileline() << ": internal error: "
		  "incomprehensible wait expression width (0)." << endl;
	    return 0;
      }

      if (expr->expr_width() > 1) {
	    assert(expr->expr_width() > 1);
	    NetEUReduce*cmp = new NetEUReduce('|', expr);
	    expr = cmp;
      }

	/* precalculate as much as possible of the wait expression. */
      eval_expr(expr);

	/* Detect the unusual case that the wait expression is
	   constant. Constant true is OK (it becomes transparent) but
	   constant false is almost certainly not what is intended. */
      assert(expr->expr_width() == 1);
      if (NetEConst*ce = dynamic_cast<NetEConst*>(expr)) {
	    verinum val = ce->value();
	    assert(val.len() == 1);

	      /* Constant true -- wait(1) <s1> reduces to <s1>. */
	    if (val[0] == verinum::V1) {
		  delete expr;
		  assert(enet);
		  return enet;
	    }

	      /* Otherwise, false. wait(0) blocks permanently. */

	    cerr << get_fileline() << ": warning: wait expression is "
		 << "constant false." << endl;
	    cerr << get_fileline() << ":        : The statement will "
		 << "block permanently." << endl;

	      /* Create an event wait and an otherwise unreferenced
		 event variable to force a perpetual wait. */
	    NetEvent*wait_event = new NetEvent(scope->local_symbol());
	    scope->add_event(wait_event);

	    NetEvWait*wait = new NetEvWait(0);
	    wait->add_event(wait_event);
	    wait->set_line(*this);

	    delete expr;
	    delete enet;
	    return wait;
      }

	/* Invert the sense of the test with an exclusive NOR. In
	   other words, if this adjusted expression returns TRUE, then
	   wait. */
      assert(expr->expr_width() == 1);
      expr = new NetEBComp('N', expr, new NetEConst(verinum(verinum::V1)));
      eval_expr(expr);

      NetEvent*wait_event = new NetEvent(scope->local_symbol());
      scope->add_event(wait_event);

      NetEvWait*wait = new NetEvWait(0 /* noop */);
      wait->add_event(wait_event);
      wait->set_line(*this);

      NexusSet*wait_set = expr->nex_input();
      if (wait_set == 0) {
	    cerr << get_fileline() << ": internal error: No NexusSet"
		 << " from wait expression." << endl;
	    des->errors += 1;
	    return 0;
      }

      if (wait_set->count() == 0) {
	    cerr << get_fileline() << ": internal error: Empty NexusSet"
		 << " from wait expression." << endl;
	    des->errors += 1;
	    return 0;
      }

      NetEvProbe*wait_pr = new NetEvProbe(scope, scope->local_symbol(),
					  wait_event, NetEvProbe::ANYEDGE,
					  wait_set->count());
      for (unsigned idx = 0; idx < wait_set->count() ;  idx += 1)
	    connect(wait_set[0][idx], wait_pr->pin(idx));

      delete wait_set;
      des->add_node(wait_pr);

      NetWhile*loop = new NetWhile(expr, wait);
      loop->set_line(*this);

	/* If there is no real substatement (i.e., "wait (foo) ;") then
	   we are done. */
      if (enet == 0)
	    return loop;

	/* Create a sequential block to combine the wait loop and the
	   delayed statement. */
      NetBlock*block = new NetBlock(NetBlock::SEQU, 0);
      block->append(loop);
      block->append(enet);
      block->set_line(*this);

      return block;
}


NetProc* PEventStatement::elaborate(Design*des, NetScope*scope) const
{
      NetProc*enet = 0;
      if (statement_) {
	    enet = statement_->elaborate(des, scope);
	    if (enet == 0)
		  return 0;

      } else {
	    enet = new NetBlock(NetBlock::SEQU, 0);
	    enet->set_line(*this);
      }

      if ((expr_.count() == 1) && (expr_[0]->type() == PEEvent::POSITIVE))
	    return elaborate_wait(des, scope, enet);

      return elaborate_st(des, scope, enet);
}

/*
 * Forever statements are represented directly in the netlist. It is
 * theoretically possible to use a while structure with a constant
 * expression to represent the loop, but why complicate the code
 * generators so?
 */
NetProc* PForever::elaborate(Design*des, NetScope*scope) const
{
      NetProc*stat = statement_->elaborate(des, scope);
      if (stat == 0) return 0;

      NetForever*proc = new NetForever(stat);
      return proc;
}

/*
 * Force is like a procedural assignment, most notably procedural
 * continuous assignment:
 *
 *    force <lval> = <rval>
 *
 * The <lval> can be anything that a normal behavioral assignment can
 * take, plus net signals. This is a little bit more lax then the
 * other procedural assignments.
 */
NetForce* PForce::elaborate(Design*des, NetScope*scope) const
{
      NetForce*dev = 0;
      assert(scope);

      NetAssign_*lval = lval_->elaborate_lval(des, scope, true);
      if (lval == 0)
	    return 0;

      unsigned lwid = count_lval_width(lval);

      NetExpr*rexp = elab_and_eval(des, scope, expr_, lwid);
      if (rexp == 0)
	    return 0;

      rexp->set_width(lwid, true);
      rexp = pad_to_width(rexp, lwid);

      dev = new NetForce(lval, rexp);

      if (debug_elaborate) {
	    cerr << get_fileline() << ": debug: Elaborate force,"
		 << " lval width=" << lval->lwidth()
		 << " rval width=" << rexp->expr_width()
		 << " rval=" << *rexp
		 << endl;
      }

      dev->set_line(*this);
      return dev;
}

/*
 * elaborate the for loop as the equivalent while loop. This eases the
 * task for the target code generator. The structure is:
 *
 *     begin : top
 *       name1_ = expr1_;
 *       while (cond_) begin : body
 *          statement_;
 *          name2_ = expr2_;
 *       end
 *     end
 */
NetProc* PForStatement::elaborate(Design*des, NetScope*scope) const
{
      NetExpr*etmp;
      assert(scope);

      const PEIdent*id1 = dynamic_cast<const PEIdent*>(name1_);
      assert(id1);
      const PEIdent*id2 = dynamic_cast<const PEIdent*>(name2_);
      assert(id2);

      NetBlock*top = new NetBlock(NetBlock::SEQU, 0);
      top->set_line(*this);

	/* make the expression, and later the initial assignment to
	   the condition variable. The statement in the for loop is
	   very specifically an assignment. */
      NetNet*sig = des->find_signal(scope, id1->path());
      if (sig == 0) {
	    cerr << id1->get_fileline() << ": register ``" << id1->path()
		 << "'' unknown in " << scope_path(scope) << "." << endl;
	    des->errors += 1;
	    return 0;
      }
      assert(sig);
      NetAssign_*lv = new NetAssign_(sig);

	/* Calculate the width of the initialization as if this were
	   any other assignment statement. */
      unsigned use_width = lv->lwidth();
      bool unsized_flag = false;
      use_width = expr1_->test_width(des, scope, use_width, use_width, unsized_flag);

	/* Make the r-value of the initial assignment, and size it
	   properly. Then use it to build the assignment statement. */
      etmp = elab_and_eval(des, scope, expr1_, use_width);
      etmp->set_width(use_width);
      etmp = pad_to_width(etmp, use_width);

      if (debug_elaborate) {
	    cerr << get_fileline() << ": debug: FOR initial assign: "
		 << sig->name() << " = " << *etmp << endl;
	    assert(etmp->expr_width() >= lv->lwidth());
      }

	/* Based on the specific type of the l-value, do cleanup
	   processing on the r-value. */
      if (etmp->expr_type() != IVL_VT_REAL) {
	    unsigned wid = count_lval_width(lv);
	    etmp->set_width(wid);
	    etmp = pad_to_width(etmp, wid);
	    assert(etmp->expr_width() >= wid);
      }

      NetAssign*init = new NetAssign(lv, etmp);
      init->set_line(*this);

      top->append(init);

      NetBlock*body = new NetBlock(NetBlock::SEQU, 0);
      body->set_line(*this);

	/* Elaborate the statement that is contained in the for
	   loop. If there is an error, this will return 0 and I should
	   skip the append. No need to worry, the error has been
	   reported so it's OK that the netlist is bogus. */
      NetProc*tmp = statement_->elaborate(des, scope);
      if (tmp)
	    body->append(tmp);


	/* Elaborate the increment assignment statement at the end of
	   the for loop. This is also a very specific assignment
	   statement. Put this into the "body" block. */
      sig = des->find_signal(scope, id2->path());
      if (sig == 0) {
	    cerr << get_fileline() << ": error: Unable to find variable "
		 << id2->path() << " in for-loop increment expression." << endl;
	    des->errors += 1;
	    return body;
      }

      assert(sig);
      lv = new NetAssign_(sig);

	/* Make the rvalue of the increment expression, and size it
	   for the lvalue. */
      etmp = elab_and_eval(des, scope, expr2_, lv->lwidth());
      NetAssign*step = new NetAssign(lv, etmp);
      step->set_line(*this);

      body->append(step);


	/* Elaborate the condition expression. Try to evaluate it too,
	   in case it is a constant. This is an interesting case
	   worthy of a warning. */
      NetExpr*ce = elab_and_eval(des, scope, cond_, -1);
      if (ce == 0) {
	    delete top;
	    return 0;
      }

      if (dynamic_cast<NetEConst*>(ce)) {
	    cerr << get_fileline() << ": warning: condition expression "
		  "of for-loop is constant." << endl;
      }


	/* All done, build up the loop. */

      NetWhile*loop = new NetWhile(ce, body);
      loop->set_line(*this);
      top->append(loop);
      return top;
}

/*
 * (See the PTask::elaborate methods for basic common stuff.)
 *
 * The return value of a function is represented as a reg variable
 * within the scope of the function that has the name of the
 * function. So for example with the function:
 *
 *    function [7:0] incr;
 *      input [7:0] in1;
 *      incr = in1 + 1;
 *    endfunction
 *
 * The scope of the function is <parent>.incr and there is a reg
 * variable <parent>.incr.incr. The elaborate_1 method is called with
 * the scope of the function, so the return reg is easily located.
 *
 * The function parameters are all inputs, except for the synthetic
 * output parameter that is the return value. The return value goes
 * into port 0, and the parameters are all the remaining ports.
 */

void PFunction::elaborate(Design*des, NetScope*scope) const
{
      NetFuncDef*def = scope->func_def();
      if (def == 0) {
	    cerr << get_fileline() << ": internal error: "
		 << "No function definition for function "
		 << scope_path(scope) << endl;
	    des->errors += 1;
	    return;
      }

      assert(def);

      NetProc*st = statement_->elaborate(des, scope);
      if (st == 0) {
	    cerr << statement_->get_fileline() << ": error: Unable to elaborate "
		  "statement in function " << scope->basename() << "." << endl;
	    des->errors += 1;
	    return;
      }

      def->set_proc(st);
}

NetProc* PRelease::elaborate(Design*des, NetScope*scope) const
{
      assert(scope);

      NetAssign_*lval = lval_->elaborate_lval(des, scope, true);
      if (lval == 0)
	    return 0;

      NetRelease*dev = new NetRelease(lval);
      dev->set_line( *this );
      return dev;
}

NetProc* PRepeat::elaborate(Design*des, NetScope*scope) const
{
      assert(scope);

      NetExpr*expr = elab_and_eval(des, scope, expr_, -1);
      if (expr == 0) {
	    cerr << get_fileline() << ": Unable to elaborate"
		  " repeat expression." << endl;
	    des->errors += 1;
	    return 0;
      }

      NetProc*stat = statement_->elaborate(des, scope);
      if (stat == 0) return 0;

	// If the expression is a constant, handle certain special
	// iteration counts.
      if (NetEConst*ce = dynamic_cast<NetEConst*>(expr)) {
	    verinum val = ce->value();
	    switch (val.as_ulong()) {
		case 0:
		  delete expr;
		  delete stat;
		  return new NetBlock(NetBlock::SEQU, 0);
		case 1:
		  delete expr;
		  return stat;
		default:
		  break;
	    }
      }

      NetRepeat*proc = new NetRepeat(expr, stat);
      return proc;
}

/*
 * A task definition is elaborated by elaborating the statement that
 * it contains, and connecting its ports to NetNet objects. The
 * netlist doesn't really need the array of parameters once elaboration
 * is complete, but this is the best place to store them.
 *
 * The first elaboration pass finds the reg objects that match the
 * port names, and creates the NetTaskDef object. The port names are
 * in the form task.port.
 *
 *      task foo;
 *        output blah;
 *        begin <body> end
 *      endtask
 *
 * So in the foo example, the PWire objects that represent the ports
 * of the task will include a foo.blah for the blah port. This port is
 * bound to a NetNet object by looking up the name. All of this is
 * handled by the PTask::elaborate_sig method and the results stashed
 * in the created NetTaskDef attached to the scope.
 *
 * Elaboration pass 2 for the task definition causes the statement of
 * the task to be elaborated and attached to the NetTaskDef object
 * created in pass 1.
 *
 * NOTE: I am not sure why I bothered to prepend the task name to the
 * port name when making the port list. It is not really useful, but
 * that is what I did in pform_make_task_ports, so there it is.
 */

void PTask::elaborate(Design*des, NetScope*task) const
{
	// Elaborate any processes that are part of this scope that
	// aren't the definition itself. This can happen, for example,
	// with variable initialization statements in this scope.
      elaborate_behaviors_(des, task);

      NetTaskDef*def = task->task_def();
      assert(def);

      NetProc*st;
      if (statement_ == 0) {
	    st = new NetBlock(NetBlock::SEQU, 0);

      } else {

	    st = statement_->elaborate(des, task);
	    if (st == 0) {
		  cerr << statement_->get_fileline() << ": Unable to elaborate "
			"statement in task " << scope_path(task)
		       << " at " << get_fileline() << "." << endl;
		  return;
	    }
      }

      def->set_proc(st);
}

NetProc* PTrigger::elaborate(Design*des, NetScope*scope) const
{
      assert(scope);

      NetNet*       sig = 0;
      const NetExpr*par = 0;
      NetEvent*     eve = 0;

      NetScope*found_in = symbol_search(des, scope, event_,
					sig, par, eve);

      if (found_in == 0) {
	    cerr << get_fileline() << ": error: event <" << event_ << ">"
		 << " not found." << endl;
	    des->errors += 1;
	    return 0;
      }

      if (eve == 0) {
	    cerr << get_fileline() << ": error:  <" << event_ << ">"
		 << " is not a named event." << endl;
	    des->errors += 1;
	    return 0;
      }

      NetEvTrig*trig = new NetEvTrig(eve);
      trig->set_line(*this);
      return trig;
}

/*
 * The while loop is fairly directly represented in the netlist.
 */
NetProc* PWhile::elaborate(Design*des, NetScope*scope) const
{
      NetWhile*loop = new NetWhile(elab_and_eval(des, scope, cond_, -1),
				   statement_->elaborate(des, scope));
      return loop;
}

bool PProcess::elaborate(Design*des, NetScope*scope) const
{
      NetProc*cur = statement_->elaborate(des, scope);
      if (cur == 0) {
	    return false;
      }

      NetProcTop*top=0;
      switch (type()) {
	  case PProcess::PR_INITIAL:
	    top = new NetProcTop(scope, NetProcTop::KINITIAL, cur);
	    break;
	  case PProcess::PR_ALWAYS:
	    top = new NetProcTop(scope, NetProcTop::KALWAYS, cur);
	    break;
      }
      ivl_assert(*this, top);

	// Evaluate the attributes for this process, if there
	// are any. These attributes are to be attached to the
	// NetProcTop object.
      struct attrib_list_t*attrib_list = 0;
      unsigned attrib_list_n = 0;
      attrib_list = evaluate_attributes(attributes, attrib_list_n, des, scope);

      for (unsigned adx = 0 ;  adx < attrib_list_n ;  adx += 1)
	    top->attribute(attrib_list[adx].key,
			   attrib_list[adx].val);

      delete[]attrib_list;

      top->set_line(*this);
      des->add_process(top);

	/* Detect the special case that this is a combinational
	always block. We want to attach an _ivl_schedule_push
	attribute to this process so that it starts up and
	gets into its wait statement before non-combinational
	code is executed. */
      do {
	    if (top->type() != NetProcTop::KALWAYS)
		  break;

	    NetEvWait*st = dynamic_cast<NetEvWait*>(top->statement());
	    if (st == 0)
		  break;

	    if (st->nevents() != 1)
		  break;

	    NetEvent*ev = st->event(0);

	    if (ev->nprobe() == 0)
		  break;

	    bool anyedge_test = true;
	    for (unsigned idx = 0 ;  anyedge_test && (idx<ev->nprobe())
		       ; idx += 1) {
		  const NetEvProbe*pr = ev->probe(idx);
		  if (pr->edge() != NetEvProbe::ANYEDGE)
			anyedge_test = false;
	    }

	    if (! anyedge_test)
		  break;

	    top->attribute(perm_string::literal("_ivl_schedule_push"),
			   verinum(1));
      } while (0);

	/* If this is an always block and we have no or zero delay then
	 * a runtime infinite loop will happen. If we possible have some
	 * delay then print a warning that an infinite loop is possible.
	 */
      if (type() == PProcess::PR_ALWAYS) {
	    DelayType dly_type = top->statement()->delay_type();

	    if (dly_type == NO_DELAY || dly_type == ZERO_DELAY) {
		  cerr << get_fileline() << ": error: always statement"
		       << " does not have any delay." << endl;
		  cerr << get_fileline() << ":      : A runtime infinite"
		       << " loop will occur." << endl;
		  des->errors += 1;
		  return false;

	    } else if (dly_type == POSSIBLE_DELAY && warn_inf_loop) {
		  cerr << get_fileline() << ": warning: always statement"
		       << " may not have any delay." << endl;
		  cerr << get_fileline() << ":        : A runtime infinite"
		       << " loop may be possible." << endl;
	    }
      }

      return true;
}

void PSpecPath::elaborate(Design*des, NetScope*scope) const
{
      uint64_t delay_value[12];
      unsigned ndelays = 0;

	/* Do not elaborate specify delay paths if this feature is
	   turned off. */
      if (!gn_specify_blocks_flag)
	    return;

      ivl_assert(*this, conditional || (condition==0));

      ndelays = delays.size();
      if (ndelays > 12)
	    ndelays = 12;

      int shift = scope->time_unit() - des->get_precision();

	/* Elaborate the delay values themselves. Remember to scale
	   them for the timescale/precision of the scope. */
      for (unsigned idx = 0 ;  idx < ndelays ;  idx += 1) {
	    PExpr*exp = delays[idx];
	    NetExpr*cur = elab_and_eval(des, scope, exp, 0);

	    if (NetEConst*cur_con = dynamic_cast<NetEConst*> (cur)) {
		  delay_value[idx] = cur_con->value().as_ulong();
		  for (int tmp = 0 ;  tmp < shift ;  tmp += 1)
			delay_value[idx] *= 10;

	    } else if (NetECReal*cur_rcon = dynamic_cast<NetECReal*>(cur)) {
		  delay_value[idx] = cur_rcon->value().as_long(shift);

	    } else {
		  cerr << get_fileline() << ": error: Path delay value "
		       << "must be constant." << endl;
		  delay_value[idx] = 0;
		  des->errors += 1;
	    }
	    delete cur;
      }

      switch (delays.size()) {
	  case 1:
	  case 2:
	  case 3:
	  case 6:
	  case 12:
	    break;
	  default:
	    cerr << get_fileline() << ": error: Incorrect delay configuration."
		 << " Given " << delays.size() << " delay expressions." << endl;
	    ndelays = 1;
	    des->errors += 1;
	    break;
      }

      NetNet*condit_sig = 0;
      if (conditional && condition) {

	    NetExpr*tmp = elab_and_eval(des, scope, condition, -1);
	    ivl_assert(*condition, tmp);

	      // FIXME: Look for constant expressions here?

	      // Get a net form.
	    condit_sig = tmp->synthesize(des);
	    ivl_assert(*condition, condit_sig);
      }

	/* Create all the various paths from the path specifier. */
      typedef std::vector<perm_string>::const_iterator str_vector_iter;
      for (str_vector_iter cur = dst.begin()
		 ; cur != dst.end() ;  cur ++) {

	    if (debug_elaborate) {
		  cerr << get_fileline() << ": debug: Path to " << (*cur);
		  if (condit_sig)
			cerr << " if " << condit_sig->name();
		  else if (conditional)
			cerr << " ifnone";
		  cerr << " from ";
	    }

	    NetNet*dst_sig = scope->find_signal(*cur);
	    if (dst_sig == 0) {
		  cerr << get_fileline() << ": error: No such wire "
		       << *cur << " in this module." << endl;
		  des->errors += 1;
		  continue;
	    }

	    if (dst_sig->port_type() != NetNet::POUTPUT
		&& dst_sig->port_type() != NetNet::PINOUT) {

		  cerr << get_fileline() << ": error: Path destination "
		       << *cur << " must be an output or inout port." << endl;
		  des->errors += 1;
	    }

	    NetDelaySrc*path = new NetDelaySrc(scope, scope->local_symbol(),
					       src.size(), condit_sig,
					       conditional);
	    path->set_line(*this);

	      // The presence of the data_source_expression indicates
	      // that this is an edge sensitive path. If so, then set
	      // the edges. Note that edge==0 is BOTH edges.
	    if (data_source_expression) {
		  if (edge >= 0) path->set_posedge();
		  if (edge <= 0) path->set_negedge();
	    }

	    switch (ndelays) {
		case 12:
		  path->set_delays(delay_value[0],  delay_value[1],
				   delay_value[2],  delay_value[3],
				   delay_value[4],  delay_value[5],
				   delay_value[6],  delay_value[7],
				   delay_value[8],  delay_value[9],
				   delay_value[10], delay_value[11]);
		  break;
		case 6:
		  path->set_delays(delay_value[0], delay_value[1],
				   delay_value[2], delay_value[3],
				   delay_value[4], delay_value[5]);
		  break;
		case 3:
		  path->set_delays(delay_value[0], delay_value[1],
				   delay_value[2]);
		  break;
		case 2:
		  path->set_delays(delay_value[0], delay_value[1]);
		  break;
		case 1:
		  path->set_delays(delay_value[0]);
		  break;
	    }

	    unsigned idx = 0;
	    for (str_vector_iter cur_src = src.begin()
		       ; cur_src != src.end() ;  cur_src ++) {
		  NetNet*src_sig = scope->find_signal(*cur_src);
		  assert(src_sig);

		  if (debug_elaborate) {
			if (cur_src != src.begin()) cerr << " and ";
			cerr << src_sig->name();
		  }

		  if ( (src_sig->port_type() != NetNet::PINPUT)
		    && (src_sig->port_type() != NetNet::PINOUT) ) {

			cerr << get_fileline() << ": error: Path source "
			     << *cur_src << " must be an input or inout port."
			     << endl;
			des->errors += 1;
		  }

		  connect(src_sig->pin(0), path->pin(idx));
		  idx += 1;
	    }
	    if (debug_elaborate) {
		  cerr << endl;
	    }

	    if (condit_sig)
		  connect(condit_sig->pin(0), path->pin(idx));

	    dst_sig->add_delay_path(path);
      }

}

static void elaborate_functions(Design*des, NetScope*scope,
				const map<perm_string,PFunction*>&funcs)
{
      typedef map<perm_string,PFunction*>::const_iterator mfunc_it_t;
      for (mfunc_it_t cur = funcs.begin()
		 ; cur != funcs.end() ;  cur ++) {

	    hname_t use_name ( (*cur).first );
	    NetScope*fscope = scope->child(use_name);
	    assert(fscope);
	    (*cur).second->elaborate(des, fscope);
      }
}

static void elaborate_tasks(Design*des, NetScope*scope,
			    const map<perm_string,PTask*>&tasks)
{
      typedef map<perm_string,PTask*>::const_iterator mtask_it_t;
      for (mtask_it_t cur = tasks.begin()
		 ; cur != tasks.end() ;  cur ++) {

	    hname_t use_name ( (*cur).first );
	    NetScope*tscope = scope->child(use_name);
	    assert(tscope);
	    (*cur).second->elaborate(des, tscope);
      }
}

/*
 * When a module is instantiated, it creates the scope then uses this
 * method to elaborate the contents of the module.
 */
bool Module::elaborate(Design*des, NetScope*scope) const
{
      bool result_flag = true;

      if (gn_specify_blocks_flag) {
	      // Elaborate specparams
	    typedef map<perm_string,PExpr*>::const_iterator specparam_it_t;
	    for (specparam_it_t cur = specparams.begin()
		       ; cur != specparams.end() ; cur ++ ) {

		  NetExpr*val = elab_and_eval(des, scope, (*cur).second, -1);
		  NetScope::spec_val_t value;

		  if (NetECReal*val_c = dynamic_cast<NetECReal*> (val)) {

			value.type     = IVL_VT_REAL;
			value.real_val = val_c->value().as_double();

			if (debug_elaborate)
			      cerr << get_fileline() << ": debug: Elaborate "
				   << "specparam " << (*cur).first
				   << " value=" << value.real_val << endl;

		  } else if (NetEConst*val_c = dynamic_cast<NetEConst*> (val)) {

			value.type    = IVL_VT_BOOL;
			value.integer = val_c->value().as_long();

			if (debug_elaborate)
			      cerr << get_fileline() << ": debug: Elaborate "
				   << "specparam " << (*cur).first
				   << " value=" << value.integer << endl;

		  } else {
			value.type = IVL_VT_NO_TYPE;
			cerr << (*cur).second->get_fileline() << ": error: "
			     << "specparam " << (*cur).first << " value"
			     << " is not constant: " << *val << endl;
			des->errors += 1;
		  }

		  assert(val);
		  delete  val;
		  scope->specparams[(*cur).first] = value;
	    }
      }

	// Elaborate within the generate blocks.
      typedef list<PGenerate*>::const_iterator generate_it_t;
      for (generate_it_t cur = generate_schemes.begin()
		 ; cur != generate_schemes.end() ; cur ++ ) {
	    (*cur)->elaborate(des, scope);
      }

	// Elaborate functions.
      elaborate_functions(des, scope, funcs);

	// Elaborate the task definitions. This is done before the
	// behaviors so that task calls may reference these, and after
	// the signals so that the tasks can reference them.
      elaborate_tasks(des, scope, tasks);

	// Get all the gates of the module and elaborate them by
	// connecting them to the signals. The gate may be simple or
	// complex.
      const list<PGate*>&gl = get_gates();

      for (list<PGate*>::const_iterator gt = gl.begin()
		 ; gt != gl.end()
		 ; gt ++ ) {

	    (*gt)->elaborate(des, scope);
      }

	// Elaborate the behaviors, making processes out of them. This
	// involves scanning the PProcess* list, creating a NetProcTop
	// for each process.
      result_flag &= elaborate_behaviors_(des, scope);

	// Elaborate the specify paths of the module.

      for (list<PSpecPath*>::const_iterator sp = specify_paths.begin()
		 ; sp != specify_paths.end() ;  sp ++) {

	    (*sp)->elaborate(des, scope);
      }

      return result_flag;
}

bool PGenerate::elaborate(Design*des, NetScope*container) const
{
      bool flag = true;

	// Handle the special case that this is a CASE scheme. In this
	// case the PGenerate itself does not have the generated
	// item. Look instead for the case ITEM that has a scope
	// generated for it.
      if (scheme_type == PGenerate::GS_CASE) {
	    if (debug_elaborate)
		  cerr << get_fileline() << ": debug: generate case"
		       << " elaborating in scope "
		       << scope_path(container) << "." << endl;

	    typedef list<PGenerate*>::const_iterator generate_it_t;
	    for (generate_it_t cur = generate_schemes.begin()
		       ; cur != generate_schemes.end() ; cur ++) {
		  PGenerate*item = *cur;
		  if (! item->scope_list_.empty()) {
			flag &= item->elaborate(des, container);
		  }
	    }
	    return flag;
      }

      typedef list<NetScope*>::const_iterator scope_list_it_t;
      for (scope_list_it_t cur = scope_list_.begin()
		 ; cur != scope_list_.end() ; cur ++ ) {

	    NetScope*scope = *cur;
	      // Check that this scope is one that is contained in the
	      // container that the caller passed in.
	    if (scope->parent() != container)
		  continue;

	      // If this was an unnamed generate block, replace its
	      // temporary name with a name generated using the naming
	      // scheme defined in the Verilog-2005 standard.
	    const char*name = scope_name.str();
	    if (name[0] == '$') {
		  if (!scope->auto_name("genblk", '0', name + 4)) {
			cerr << get_fileline() << ": warning: Couldn't build"
			     << " unique name for unnamed generate block"
			     << " - using internal name " << name << endl;
		  }
	    }
	    if (debug_elaborate)
		  cerr << get_fileline() << ": debug: Elaborate in "
		       << "scope " << scope_path(scope) << endl;

	    flag = elaborate_(des, scope) & flag;
      }

      return flag;
}

bool PGenerate::elaborate_(Design*des, NetScope*scope) const
{
      elaborate_functions(des, scope, funcs);
      elaborate_tasks(des, scope, tasks);

      typedef list<PGate*>::const_iterator gates_it_t;
      for (gates_it_t cur = gates.begin() ; cur != gates.end() ; cur ++ )
	    (*cur)->elaborate(des, scope);

      typedef list<PProcess*>::const_iterator proc_it_t;
      for (proc_it_t cur = behaviors.begin(); cur != behaviors.end(); cur++)
	    (*cur)->elaborate(des, scope);

      typedef list<PGenerate*>::const_iterator generate_it_t;
      for (generate_it_t cur = generate_schemes.begin()
		 ; cur != generate_schemes.end() ; cur ++ ) {
	    (*cur)->elaborate(des, scope);
      }

      return true;
}

bool PScope::elaborate_behaviors_(Design*des, NetScope*scope) const
{
      bool result_flag = true;

	// Elaborate the behaviors, making processes out of them. This
	// involves scanning the PProcess* list, creating a NetProcTop
	// for each process.
      for (list<PProcess*>::const_iterator st = behaviors.begin()
		 ; st != behaviors.end() ; st ++ ) {

	    result_flag &= (*st)->elaborate(des, scope);
      }

      return result_flag;
}

struct root_elem {
      Module *mod;
      NetScope *scope;
};

class elaborate_root_scope_t : public elaborator_work_item_t {
    public:
      elaborate_root_scope_t(Design*des, NetScope*scope, Module*rmod)
      : elaborator_work_item_t(des), scope_(scope), rmod_(rmod)
      { }

      ~elaborate_root_scope_t() { }

      virtual void elaborate_runrun()
      {
	    Module::replace_t stub;
	    if (! rmod_->elaborate_scope(des, scope_, stub))
		  des->errors += 1;
      }

    private:
      NetScope*scope_;
      Module*rmod_;
};

class top_defparams : public elaborator_work_item_t {
    public:
      top_defparams(Design*des)
      : elaborator_work_item_t(des)
      { }

      ~top_defparams() { }

      virtual void elaborate_runrun()
      {
	      // This method recurses through the scopes, looking for
	      // defparam assignments to apply to the parameters in the
	      // various scopes. This needs to be done after all the scopes
	      // and basic parameters are taken care of because the defparam
	      // can assign to a parameter declared *after* it.
	    des->run_defparams();

	      // At this point, all parameter overrides are done. Scan the
	      // scopes and evaluate the parameters all the way down to
	      // constants.
	    des->evaluate_parameters();
      }
};

class later_defparams : public elaborator_work_item_t {
    public:
      later_defparams(Design*des)
      : elaborator_work_item_t(des)
      { }

      ~later_defparams() { }

      virtual void elaborate_runrun()
      {
	    list<NetScope*>tmp_list;
	    for (set<NetScope*>::iterator cur = des->defparams_later.begin()
		       ; cur != des->defparams_later.end() ; cur ++ )
		  tmp_list.push_back(*cur);

	    des->defparams_later.clear();

	    while (! tmp_list.empty()) {
		  NetScope*cur = tmp_list.front();
		  tmp_list.pop_front();
		  cur->run_defparams_later(des);
	    }
	    des->evaluate_parameters();
      }
};

/*
 * This function is the root of all elaboration. The input is the list
 * of root module names. The function locates the Module definitions
 * for each root, does the whole elaboration sequence, and fills in
 * the resulting Design.
 */
Design* elaborate(list<perm_string>roots)
{
      svector<root_elem*> root_elems(roots.size());
      bool rc = true;
      unsigned i = 0;

	// This is the output design. I fill it in as I scan the root
	// module and elaborate what I find.
      Design*des = new Design;

	// Scan the root modules by name, and elaborate their scopes.
      for (list<perm_string>::const_iterator root = roots.begin()
		 ; root != roots.end()
		 ; root++) {

	      // Look for the root module in the list.
	    map<perm_string,Module*>::const_iterator mod = pform_modules.find(*root);
	    if (mod == pform_modules.end()) {
		  cerr << "error: Unable to find the root module \""
		       << (*root) << "\" in the Verilog source." << endl;
		  cerr << "     : Perhaps ``-s " << (*root)
		       << "'' is incorrect?" << endl;
		  des->errors++;
		  continue;
	    }

	      // Get the module definition for this root instance.
	    Module *rmod = (*mod).second;

	      // Make the root scope. This makes a NetScoep object and
	      // pushes it into the list of root scopes in the Design.
	    NetScope*scope = des->make_root_scope(*root);

	      // Collect some basic properties of this scope from the
	      // Module definition.
	    scope->set_line(rmod);
	    scope->time_unit(rmod->time_unit);
	    scope->time_precision(rmod->time_precision);
	    scope->default_nettype(rmod->default_nettype);
	    des->set_precision(rmod->time_precision);


	      // Save this scope, along with its defintion, in the
	      // "root_elems" list for later passes.
	    struct root_elem *r = new struct root_elem;
	    r->mod = rmod;
	    r->scope = scope;
	    root_elems[i++] = r;

	      // Arrange for these scopes to be elaborated as root
	      // scopes. Create an "elaborate_root_scope" object to
	      // contain the work item, and append it to the scope
	      // elaborations work list.
	    elaborator_work_item_t*es = new elaborate_root_scope_t(des, scope, rmod);
	    des->elaboration_work_list.push_back(es);
      }

	// After the work items for the root scope elaboration, push a
	// work item to process the defparams.
      des->elaboration_work_list.push_back(new top_defparams(des));

	// Run the work list of scope elaborations until the list is
	// empty. This list is initially populated above where the
	// initial root scopes are primed.
      while (! des->elaboration_work_list.empty()) {
	      // Transfer the queue to a temporary queue.
	    list<elaborator_work_item_t*> cur_queue;
	    while (! des->elaboration_work_list.empty()) {
		  cur_queue.push_back(des->elaboration_work_list.front());
		  des->elaboration_work_list.pop_front();
	    }

	      // Run from the temporary queue. If the temporary queue
	      // items create new work queue items, they will show up
	      // in the elaboration_work_list and then we get to run
	      // through them in the next pass.
	    while (! cur_queue.empty()) {
		  elaborator_work_item_t*tmp = cur_queue.front();
		  cur_queue.pop_front();
		  tmp->elaborate_runrun();
		  delete tmp;
	    }

	    if (! des->elaboration_work_list.empty()) {
		  des->elaboration_work_list.push_back(new later_defparams(des));
	    }
      }

	// Look for residual defparams (that point to a non-existent
	// scope) and clean them out.
      des->residual_defparams();

	// Errors already? Probably missing root modules. Just give up
	// now and return nothing.
      if (des->errors > 0)
	    return des;

	// With the parameters evaluated down to constants, we have
	// what we need to elaborate signals and memories. This pass
	// creates all the NetNet and NetMemory objects for declared
	// objects.
      for (i = 0; i < root_elems.count(); i++) {
	    Module *rmod = root_elems[i]->mod;
	    NetScope *scope = root_elems[i]->scope;

	    if (! rmod->elaborate_sig(des, scope)) {
		  delete des;
		  return 0;
	    }
      }

	// Now that the structure and parameters are taken care of,
	// run through the pform again and generate the full netlist.
      for (i = 0; i < root_elems.count(); i++) {
	    Module *rmod = root_elems[i]->mod;
	    NetScope *scope = root_elems[i]->scope;

	    rc &= rmod->elaborate(des, scope);
      }


      if (rc == false) {
	    delete des;
	    des = 0;
      }

      return des;
}
