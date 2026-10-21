-- expect: 4 package p
-- expect: 9 package after_attr_units
-- Attribute specification with entity class "units".
package p is
  attribute a : integer;
  attribute a of t : units is 1;
end package p;

package after_attr_units is
end package;
