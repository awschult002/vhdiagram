-- expect: 4 architecture rtl
-- expect: 18 entity after_case
-- Sequential case in a process: "end loop l;" then "when" is not a generate alternative end.
architecture rtl of e is
begin
  p: process (all) begin
    case s is
      when 0 =>
        l: for i in 0 to 1 loop
        end loop l;
      when 1 =>
        if a then null; end if;
      when others => null;
    end case;
  end process;
end architecture;

entity after_case is end;
