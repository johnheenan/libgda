/* -*- Mode: Vala; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * libgdadata
 * Copyright (C) Daniel Espinosa Ortiz 2011 <esodan@gmail.com>
 * 
 * libgda is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * libgda is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

using Gee;
using Gda;

namespace GdaData
{
	public class SqlWhere<Expression> : GLib.Object, Traversable<Expression>, Iterable<Expression>, Collection<Expression>
	{
		/**
		 * Verify that all expressions are valid in the context of the given database collection
		 */
		public static Collection<Expression> verify (DbCollection db) 
		{
			var l = new ArrayList<Expression> ();
			var iter = this.iterator ();
			while (iter.next ())
			{
				var e = iter.get ();
				if (!e.verify (db))
					l.add (e);
			}
			return (Collection) l;
		}
	}
}
