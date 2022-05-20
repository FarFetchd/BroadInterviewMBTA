# Running
`g++ -std=c++2a -o mbta main.cc -lcurl && ./mbta`

You'll need to be able to link lcurl; on Ubuntu 20.04 `apt install libcurl4-openssl-dev`.

You can put an MBTA API key in api_key.txt if you want, but you don't have to.

# Testing

I just manually tested a bunch of cases. For an auto pre-submit / regression test setup, ideally I would use a sufficiently interesting topology (probably a snapshot of MBTA) built into the tests, with the code modified to allow dynamic injection of either the actual HTTP client logic hitting the MBTA API, or a fake that just returns that built-in data.

But, more simply and lazily, you could also just build the RoutePlanner object from real on-the-fly queries, and feed through a bunch of test cases. It would be just the single handful of API queries per run of the entire test suite, and given that they freely give away 1000 per minute, it would probably be ok even if it was running on every commit to a PR or whatnot. Definitely not the right way to do things, but an option if you're in a rush.

My test cases:
```
Enter 'from' station: Copley
Enter 'to' station: Mattapan
Copley to Mattapan -> Green-B, Red, Mattapan,
Enter 'from' station: Mattapan
Enter 'to' station: Copley
Mattapan to Copley -> Mattapan, Red, Green-B,
Enter 'from' station: Kendall/MIT
Enter 'to' station: Braintree
Kendall/MIT to Braintree -> Red,
Enter 'from' station: Braintree
Enter 'to' station: Kendall/MIT
Braintree to Kendall/MIT -> Red,
Enter 'from' station: Downtown Crossing
Enter 'to' station: Downtown Crossing
If you're already there, then there's nowhere to go!  <--caught a bug; was infinite loop
Enter 'from' station: Forest Hills
Enter 'to' station: Wonderland
Forest Hills to Wonderland -> Orange, Blue,
Enter 'from' station: Wonderland
Enter 'to' station: Forest Hills
Wonderland to Forest Hills -> Blue, Orange,
Enter 'from' station: Park Street
Enter 'to' station: Charles/MGH
Park Street to Charles/MGH -> Red,     <--caught a bug; was Green-B
Enter 'from' station: Charles/MGH
Enter 'to' station: Park Street
Charles/MGH to Park Street -> Red,
Enter 'from' station: North Station
Enter 'to' station: Haymarket
North Station to Haymarket -> Green-D,
Enter 'from' station: Haymarket
Enter 'to' station: North Station
Haymarket to North Station -> Green-D,
Enter 'from' station: Alewife
Enter 'to' station: Davis
Alewife to Davis -> Red,
Enter 'from' station: Davis
Enter 'to' station: Alewife
Davis to Alewife -> Red,
Enter 'from' station: Boston College
Enter 'to' station: Mattapan
Boston College to Mattapan -> Green-B, Red, Mattapan,
Enter 'from' station: Mattapan
Enter 'to' station: Boston College
Mattapan to Boston College -> Mattapan, Red, Green-B,
Enter 'from' station: Boston College
Enter 'to' station: Braintree
Boston College to Braintree -> Green-B, Red,
Enter 'from' station: Braintree
Enter 'to' station: Boston College
Braintree to Boston College -> Red, Green-B,
```

# Notes and improvements

I wouldn't keep api_key.txt in here with the code with just .gitignore protecting it from getting out into the wild. How you actually do it depends on the context; if it's a user interactive thing maybe they should enter it on stdin, if it's part of big fancy machinery, you probably already have some framework set up to grant access to secrets based on whatever credentials.

Could maybe use the fields[] query param to not receive irrelevant stuff in the queries, especially the queries about stops. This would be relevant if 1) the retrieved data was measured in more than KBs, or 2) the frequency of query was "something per second" rather than "something per hour".

Weird: using "https://api-v3.mbta.com/routes?filter[type]=0,1&include=line" to get line IDs, they showed me ID strings of the form line-Blue. But that didn't work. I ultimately found https://groups.google.com/g/massdotdevelopers/c/WiJUyGIpHdI which led me to try just "Blue", and yup, that worked.

I investigated to see if the data they return includes any helpful info about where routes have connections, but looks like no; need to construct that ourselves from overlapping station IDs.
