-- expect: 4 package q
-- expect: 9 package after_attr_package
-- Attribute specification with entity class "package".
package q is
  attribute a : string;
  attribute a of q : package is "x";
end package q;

package after_attr_package is
end package;
