-- expect: 5 package p
-- expect: 15 package after_attr_subprogram
-- Attribute specification for a subprogram whose value is not a plain
-- name: "function is <literal|aggregate|selected name>" is not a body.
package p is
  function f return bit;
  procedure q;
  attribute a : string;
  attribute a of f : function is "x";
  attribute b of q : procedure is 1;
  attribute c of f : function is (1, 2);
  attribute d of f : function is work.k;
end package p;

package after_attr_subprogram is
end package;
